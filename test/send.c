#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>

#include "util.h"
#include "net.h"
#include "ip.h"
#include "icmp.h"
#include "driver/ether_tap.h"

#include "driver/loopback.h"

#include "test.h"

static volatile sig_atomic_t terminate;

static void
on_signal(int s)
{
    (void)s;
    terminate = 1;
}

static int
setup(void)
{
    struct net_device *dev;
    struct ip_iface *iface;

    signal(SIGINT, on_signal);
    // プロトコルの登録
    // net, ipの2種類ある
    if (net_init() == -1) {
        errorf("net_init() failure");
        return -1;
    }
    // ループバックデバイスの初期化と登録
    // devはifaceをメンバーに持つ。ifaceは対応するIPアドレスやネットマスクを持つ
    dev = loopback_init();
    if (!dev) {
        errorf("loopback_init() failure");
        return -1;
    }
    iface = ip_iface_alloc(LOOPBACK_IP_ADDR, LOOPBACK_NETMASK);
    if (!iface) {
        errorf("ip_iface_alloc() failure");
        return -1;
    }
    if (ip_iface_register(dev, iface) == -1) {
        errorf("ip_iface_register() failure");
        return -1;
    }

    // イーサネットデバイスの初期化と登録
    // IRQの設定もする
    dev = ether_tap_init(ETHER_TAP_NAME, ETHER_TAP_HW_ADDR);
    if (!dev) {
        errorf("ether_tap_init() failure");
        return -1;
    }
    iface = ip_iface_alloc(ETHER_TAP_IP_ADDR, ETHER_TAP_NETMASK);
    if (!iface) {
        errorf("ip_iface_alloc() failure");
        return -1;
    }
    if (ip_iface_register(dev, iface) == -1) {
        errorf("ip_iface_register() failure");
        return -1;
    }
    // デフォルトゲートウェイの設定
    if (ip_route_set_default_gateway(iface, DEFAULT_GATEWAY) == -1) {
        errorf("ip_route_set_default_gateway() failure");
        return -1;
    }
    // 割込みスレッドの生成とデバイスのオープン
    if (net_run() == -1) {
        errorf("net_run() failure");
        return -1;
    }
    return 0;
}

static void
cleanup(void)
{
    net_shutdown();
}

int
main(int argc, char *argv[])
{
    if (argc != 2) {
        errorf("invalid args number");
        return -1;
    }
    ip_addr_t src, dst;
    uint16_t id, seq = 0;
    size_t offset = IP_HDR_SIZE_MIN + ICMP_HDR_SIZE;

    if (setup() == -1) {
        errorf("setup() failure");
        return -1;
    }
    src = IP_ADDR_ANY;
    if (ip_addr_pton(argv[1], &dst) == -1) {
        errorf("ip_addr_pton() failure");
        return -1;
    }
    id = getpid() % UINT16_MAX;
    while (!terminate) {
        if (icmp_output(ICMP_TYPE_ECHO, 0, hton32(id << 16 || ++seq), test_data + offset, sizeof(test_data) - offset, src, dst) == -1) {
            errorf("icmp_output() failure");
            break;
        }
        sleep(1);
    }
    cleanup();
    return 0;
}
