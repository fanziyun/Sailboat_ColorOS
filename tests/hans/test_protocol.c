#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include "hans_protocol.h"

static struct hans_message valid(int type)
{
	struct hans_message msg = { 0 };
	msg.type = type;
	msg.port = 1234;
	msg.target_uid = 10001;
	return msg;
}

static void test_lengths(void)
{
	struct hans_message wire = valid(HANS_FROZEN_TRANS), decoded;
	size_t len;

	wire.persistent = HANS_CHECK_ALL;
	for (len = 0; len <= HANS_MESSAGE_SIZE + 8; len++) {
		int ret = hans_decode_message(&decoded, &wire, len);
		if (len == HANS_LEGACY_SIZE) {
			assert(!ret && decoded.persistent == 0);
		} else if (len == HANS_MESSAGE_SIZE || len == HANS_COLOROS17_SIZE) {
			assert(!ret && decoded.persistent == HANS_CHECK_ALL);
		} else {
			assert(ret == -EMSGSIZE);
		}
	}
	assert(hans_decode_message(NULL, &wire, sizeof(wire)) == -EMSGSIZE);
	assert(hans_decode_message(&decoded, NULL, sizeof(wire)) == -EMSGSIZE);
}

static void test_values(void)
{
	struct hans_message wire, decoded;
	int type, cmd, persistent;

	for (type = -2; type < HANS_TYPE_MAX + 2; type++) {
		wire = valid(type);
		int ret = hans_decode_message(&decoded, &wire, sizeof(wire));
		if (type < 0 || type >= HANS_TYPE_MAX)
			assert(ret == -EINVAL);
		else if (type == HANS_LOOP_BACK || type == HANS_PKG ||
			 type == HANS_FROZEN_TRANS || type == HANS_CPUCTL_TRANS)
			assert(!ret);
		else
			assert(ret == -EOPNOTSUPP);
	}
	wire = valid(HANS_LOOP_BACK);
	wire.port = 0;
	assert(hans_decode_message(&decoded, &wire, sizeof(wire)) == -EINVAL);
	wire.port = INT_MIN;
	assert(hans_decode_message(&decoded, &wire, sizeof(wire)) == -EINVAL);
	for (cmd = -1; cmd <= HANS_PKG_CMD_MAX; cmd++) {
		for (persistent = -1; persistent <= 3; persistent++) {
			wire = valid(HANS_PKG);
			wire.pkg_cmd = cmd;
			wire.persistent = persistent;
			int ret = hans_decode_message(&decoded, &wire, sizeof(wire));
			assert((ret == 0) == (cmd >= 0 && cmd < HANS_PKG_CMD_MAX &&
				persistent >= 0 && persistent <= 2));
		}
	}
	wire = valid(HANS_PKG);
	wire.target_uid = 9999;
	assert(hans_decode_message(&decoded, &wire, sizeof(wire)) == -EINVAL);
	wire.pkg_cmd = HANS_DEL_ALL;
	assert(!hans_decode_message(&decoded, &wire, sizeof(wire)));
	wire = valid(HANS_FROZEN_TRANS);
	wire.persistent = 1;
	assert(hans_decode_message(&decoded, &wire, sizeof(wire)) == -EINVAL);
	wire.target_uid = HANS_CHECK_CGRPV2;
	assert(!hans_decode_message(&decoded, &wire, sizeof(wire)));
	wire.target_uid = HANS_SKIP_REPLY_ON;
	assert(!hans_decode_message(&decoded, &wire, sizeof(wire)));
	wire.target_uid = HANS_SKIP_REPLY_OFF;
	assert(!hans_decode_message(&decoded, &wire, sizeof(wire)));
	wire.target_uid = -123;
	assert(hans_decode_message(&decoded, &wire, sizeof(wire)) == -EINVAL);
}

static void test_report(void)
{
	struct hans_message msg;
	char token[300];

	memset(token, 'x', sizeof(token));
	token[sizeof(token) - 1] = 0;
	memset(&msg, 0xa5, sizeof(msg));
	hans_make_report(&msg, HANS_LOOP_BACK, -1, -1, -1, -1, "loop back", 2);
	assert(msg.port == HANS_PORT_MAGIC && msg.type == HANS_LOOP_BACK && msg.code == 2);
	assert(msg.persistent == 0 && msg.pkg_cmd == -1);
	assert(!strcmp(msg.rpc_name, "loop back"));
	for (size_t i = strlen(msg.rpc_name) + 1; i < HANS_TOKEN_SIZE; i++)
		assert(msg.rpc_name[i] == 0);
	hans_make_report(&msg, HANS_ASYNC_BINDER, 1, 2, 3, 4, token, 5);
	assert(strlen(msg.rpc_name) == HANS_TOKEN_SIZE - 1);
	assert(msg.caller_pid == 1 && msg.caller_uid == 2 && msg.target_pid == 3 &&
	       msg.target_uid == 4 && msg.code == 5 && msg.persistent == 0);
	hans_make_report(&msg, HANS_SIGNAL, 1, 2, 3, 4, NULL, 9);
	assert(msg.rpc_name[0] == 0);
}

static void test_fuzz(void)
{
	unsigned char bytes[HANS_MESSAGE_SIZE];
	struct hans_message decoded;
	uint32_t state = 0x15356;

	for (unsigned int run = 0; run < 100000; run++) {
		for (size_t i = 0; i < sizeof(bytes); i++) {
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			bytes[i] = state;
		}
		int ret = hans_decode_message(&decoded, bytes, (run & 1) ? 172 : 176);
		assert(ret <= 0);
		if (!ret) {
			assert(decoded.port > 0);
			assert(decoded.type == HANS_LOOP_BACK || decoded.type == HANS_PKG ||
			       decoded.type == HANS_FROZEN_TRANS || decoded.type == HANS_CPUCTL_TRANS);
		}
	}
}

int main(void)
{
	test_lengths();
	test_values();
	test_report();
	test_fuzz();
	puts("PASS: wire layout, 172/176 length, value checks, zeroing, 100000 fuzz inputs");
	return 0;
}
