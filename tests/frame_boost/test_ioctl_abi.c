#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "frame_ioctl.h"
#include "ua_ioctl_common.h"

_Static_assert(sizeof(void *) == 8, "native arm64 ABI requires LP64");
_Static_assert(sizeof(pid_t) == 4, "pid_t ABI");
_Static_assert(sizeof(struct ofb_ctrl_data) == 48, "ofb_ctrl_data ABI");
_Static_assert(offsetof(struct ofb_ctrl_data, stage) == 16, "stage ABI");
_Static_assert(offsetof(struct ofb_ctrl_data, vsyncNs) == 24, "vsyncNs ABI");
_Static_assert(offsetof(struct ofb_ctrl_data, capacity_need) == 32, "capacity_need ABI");
_Static_assert(sizeof(struct ofb_stune_data) == 68, "ofb_stune_data ABI");
_Static_assert(sizeof(struct ofb_ctrl_cluster) == 40, "cluster ABI");
_Static_assert(sizeof(struct ofb_key_thread_info) == 88, "key threads ABI");
_Static_assert(sizeof(struct ofb_frame_util_info) == 16, "frame util ABI");
_Static_assert(sizeof(struct ua_info_data) == 24, "UA info ABI");
_Static_assert(offsetof(struct ua_info_data, frame_prev_util_scale) == 8, "UA alignment ABI");

_Static_assert(CMD_ID_SET_FPS == 0xc030de01UL, "SET_FPS ioctl");
_Static_assert(CMD_ID_BOOST_HIT == 0xc030de02UL, "BOOST_HIT ioctl");
_Static_assert(CMD_ID_END_FRAME == 0xc030de03UL, "END_FRAME ioctl");
_Static_assert(CMD_ID_SF_FRAME_MISSED == 0xc030de04UL, "SF_FRAME_MISSED ioctl");
_Static_assert(CMD_ID_SF_COMPOSE_HINT == 0xc030de05UL, "SF_COMPOSE_HINT ioctl");
_Static_assert(CMD_ID_IS_HWUI_RT == 0xc030de06UL, "IS_HWUI_RT ioctl");
_Static_assert(CMD_ID_SET_TASK_TAGGING == 0xc030de07UL, "SET_TASK_TAGGING ioctl");
_Static_assert(CMD_ID_SET_SF_MSG_TRANS == 0xc030de08UL, "SET_SF_MSG_TRANS ioctl");
_Static_assert(CMD_ID_RELATED_SCHED_CONFIG == 0xc030de09UL, "RELATED_SCHED_CONFIG ioctl");
_Static_assert(CMD_ID_BOOST_STUNE == 0xc044de0aUL, "BOOST_STUNE ioctl");
_Static_assert(CMD_ID_BOOST_STUNE_GPU == 0xc044de0bUL, "BOOST_STUNE_GPU ioctl");
_Static_assert(CMD_ID_SET_TASK_PREFERED_CLUSTER == 0xc028df01UL, "cluster ioctl");
_Static_assert(CMD_ID_ADD_TASK_TO_GROUP == 0xc058df02UL, "group ioctl");
_Static_assert(CMD_ID_NOTIFY_FRAME_START == 0xc010df03UL, "frame start ioctl");
_Static_assert(CPU_CTRL_SET_UA_STATUS == 0xc0186f01UL, "UA status ioctl");
_Static_assert(CPU_CTRL_GET_UTIL_INFO == 0x80186f02UL, "UA util ioctl");

_Static_assert(BOOST_FRAME_START == 200, "frame start stage");
_Static_assert(BOOST_MOVE_FG == 201, "foreground stage");
_Static_assert(BOOST_FRAME_END == 204, "frame end stage");
_Static_assert(BOOST_SET_RENDER_THREAD == 213, "render thread stage");
_Static_assert(BOOST_MSG_TRANS_START == 214, "SF transaction stage");
_Static_assert(BOOST_CLIENT_COMPOSITION == 215, "SF composition stage");
_Static_assert(BOOST_SF_EXECUTE == 216, "SF execute stage");
