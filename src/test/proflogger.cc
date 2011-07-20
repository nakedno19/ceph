// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "common/ProfLogger.h"
#include "common/admin_socket_client.h"
#include "common/ceph_context.h"
#include "common/config.h"
#include "common/errno.h"
#include "common/safe_io.h"
#include "test/unit.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <map>
#include <poll.h>
#include <sstream>
#include <stdint.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

TEST(ProfLogger, SimpleTest) {
  g_ceph_context->_conf->set_val_or_die("admin_socket", get_rand_socket_path());
  g_ceph_context->_conf->apply_changes();
  AdminSocketClient client(get_rand_socket_path());
  std::string message;
  ASSERT_EQ("", client.get_message(&message));
  ASSERT_EQ("{}", message);
}

enum {
  FAKE_PROFLOGGER1_ELEMENT_FIRST = 200,
  FAKE_PROFLOGGER1_ELEMENT_1,
  FAKE_PROFLOGGER1_ELEMENT_2,
  FAKE_PROFLOGGER1_ELEMENT_3,
  FAKE_PROFLOGGER1_ELEMENT_LAST,
};

std::string sd(const char *c)
{
  std::string ret(c);
  std::string::size_type sz = ret.size();
  for (std::string::size_type i = 0; i < sz; ++i) {
    if (ret[i] == '\'') {
      ret[i] = '\"';
    }
  }
  return ret;
}

static ProfLogger* setup_fake_proflogger1(CephContext *cct)
{
  ProfLoggerBuilder bld(cct, "fake_proflogger_1",
	  FAKE_PROFLOGGER1_ELEMENT_FIRST, FAKE_PROFLOGGER1_ELEMENT_LAST);
  bld.add_u64(FAKE_PROFLOGGER1_ELEMENT_1, "element1");
  bld.add_fl(FAKE_PROFLOGGER1_ELEMENT_2, "element2");
  bld.add_fl_avg(FAKE_PROFLOGGER1_ELEMENT_3, "element3");
  return bld.create_proflogger();
}

TEST(ProfLogger, SingleProfLogger) {
  ProfLoggerCollection *coll = g_ceph_context->GetProfLoggerCollection();
  ProfLogger* fake_pf = setup_fake_proflogger1(g_ceph_context);
  coll->logger_add(fake_pf);
  g_ceph_context->_conf->set_val_or_die("admin_socket", get_rand_socket_path());
  g_ceph_context->_conf->apply_changes();
  AdminSocketClient client(get_rand_socket_path());
  std::string msg;
  ASSERT_EQ("", client.get_message(&msg));
  ASSERT_EQ(sd("{'fake_proflogger_1':{'element1':0,"
	    "'element2':0,'element3':{'count':0,'sum':0},},}"), msg);
  fake_pf->inc(FAKE_PROFLOGGER1_ELEMENT_1);
  fake_pf->fset(FAKE_PROFLOGGER1_ELEMENT_2, 0.5);
  fake_pf->finc(FAKE_PROFLOGGER1_ELEMENT_3, 100.0);
  ASSERT_EQ("", client.get_message(&msg));
  ASSERT_EQ(sd("{'fake_proflogger_1':{'element1':1,"
	    "'element2':0.5,'element3':{'count':1,'sum':100},},}"), msg);
  fake_pf->finc(FAKE_PROFLOGGER1_ELEMENT_3, 0.0);
  fake_pf->finc(FAKE_PROFLOGGER1_ELEMENT_3, 25.0);
  ASSERT_EQ("", client.get_message(&msg));
  ASSERT_EQ(sd("{'fake_proflogger_1':{'element1':1,'element2':0.5,"
	    "'element3':{'count':3,'sum':125},},}"), msg);
}

enum {
  FAKE_PROFLOGGER2_ELEMENT_FIRST = 400,
  FAKE_PROFLOGGER2_ELEMENT_FOO,
  FAKE_PROFLOGGER2_ELEMENT_BAR,
  FAKE_PROFLOGGER2_ELEMENT_LAST,
};

static ProfLogger* setup_fake_proflogger2(CephContext *cct)
{
  ProfLoggerBuilder bld(cct, "fake_proflogger_2",
	  FAKE_PROFLOGGER2_ELEMENT_FIRST, FAKE_PROFLOGGER2_ELEMENT_LAST);
  bld.add_u64(FAKE_PROFLOGGER2_ELEMENT_FOO, "foo");
  bld.add_fl(FAKE_PROFLOGGER2_ELEMENT_BAR, "bar");
  return bld.create_proflogger();
}

TEST(ProfLogger, MultipleProfloggers) {
  ProfLoggerCollection *coll = g_ceph_context->GetProfLoggerCollection();
  coll->logger_clear();
  ProfLogger* fake_pf1 = setup_fake_proflogger1(g_ceph_context);
  ProfLogger* fake_pf2 = setup_fake_proflogger2(g_ceph_context);
  coll->logger_add(fake_pf1);
  coll->logger_add(fake_pf2);
  g_ceph_context->_conf->set_val_or_die("admin_socket", get_rand_socket_path());
  g_ceph_context->_conf->apply_changes();
  AdminSocketClient client(get_rand_socket_path());
  std::string msg;

  ASSERT_EQ("", client.get_message(&msg));
  ASSERT_EQ(sd("{'fake_proflogger_1':{'element1':0,'element2':0,'element3':"
	    "{'count':0,'sum':0},},'fake_proflogger_2':{'foo':0,'bar':0,},}"), msg);

  fake_pf1->inc(FAKE_PROFLOGGER1_ELEMENT_1);
  fake_pf1->inc(FAKE_PROFLOGGER1_ELEMENT_1, 5);
  ASSERT_EQ("", client.get_message(&msg));
  ASSERT_EQ(sd("{'fake_proflogger_1':{'element1':6,'element2':0,'element3':"
	    "{'count':0,'sum':0},},'fake_proflogger_2':{'foo':0,'bar':0,},}"), msg);

  coll->logger_remove(fake_pf2);
  ASSERT_EQ("", client.get_message(&msg));
  ASSERT_EQ(sd("{'fake_proflogger_1':{'element1':6,'element2':0,"
	    "'element3':{'count':0,'sum':0},},}"), msg);

  coll->logger_clear();
  ASSERT_EQ("", client.get_message(&msg));
  ASSERT_EQ("{}", msg);
}
