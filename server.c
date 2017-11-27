/*
 * Copyright 2016 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file    send_client.c
 *
 * OES API接口库的示例程序
 *
 * @version 1.0 2016/10/21
 * @since   2016/10/21
 */


#define _GNU_SOURCE             /* See feature_test_macros(7) */

#include    <stdio.h>
#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>
#include    <errno.h>

#include    <sched.h>
#include    <pthread.h>

#include    <unistd.h>
#include    <fcntl.h>
#include    <getopt.h>
#include    <sys/time.h>
#include    <sys/types.h>
#include    <sys/socket.h>
#include    <netinet/in.h>
#include    <arpa/inet.h>


typedef int64_t         int64;
typedef int32_t         int32;
typedef int16_t         int16;
typedef uint16_t        uint16;


uint16          _port = 0;
int16           _cpu = -1;


static inline int32
_SendServer_GetCpuAffinity(void) {
#ifdef HAVE_CPU_BIND
    int32               i = 0;
    cpu_set_t           cpuset;

    CPU_ZERO(&cpuset);

    if(pthread_getaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
        fprintf(stderr, "pthread_getaffinity_np erro\n");
        return -1;
    }

    fprintf(stdout, "    CPU");
    for (i = 0; i < CPU_SETSIZE; i++) {
       if (CPU_ISSET(i, &cpuset)) {
           fprintf(stdout," %d", i);
       }
    }
    fprintf(stdout, "\n");
#endif
    return 0;
}


static inline int32
_SendServer_SetCpuAffinity(int32 i) {
#ifdef HAVE_CPU_BIND
    cpu_set_t           cpuset;

    CPU_ZERO(&cpuset);
    CPU_SET(i,&cpuset);

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset)  != 0) {
        fprintf(stderr, "pthread_setaffinity_np erro\n");
        return -1;
    }
#endif
    return 0;
}


/**
 * 发送委托请求
 *
 * 提示:
 * - 可以通过 OesApi_GetClEnvId() 方法获得到当前通道所使用的客户端环境号(clEnvId), 如:
 *   <code>int8 clEnvId = OesApi_GetClEnvId(pOrdChannel);</code>
 *
 * @param   pOrdChannel     委托通道的会话信息
 * @param   mktId           市场代码 (必填) @see eOesMarketIdT
 * @param   pSecurityId     股票代码 (必填)
 * @param   pInvAcctId      股东账户代码 (可不填)
 * @param   ordType         委托类型 (必填) @see eOesOrdTypeT, eOesOrdTypeShT, eOesOrdTypeSzT
 * @param   bsType          买卖类型 (必填) @see eOesBuySellTypeT
 * @param   ordQty          委托数量 (必填, 单位为股/张)
 * @param   ordPrice        委托价格 (必填, 单位精确到元后四位，即1元 = 10000)
 * @return  大于等于0，成功；小于0，失败（错误号）
 */
static inline int32
_SendServer_Send(int32 socket, const char *pMsg, size_t msgSize) {
    int32               sendLen = 0;
    int32               ret = 0;
    struct timeval      before = {0, 0};
    struct timeval      after = {0, 0};

    gettimeofday(&before, NULL);
    do {
        ret = send(socket, pMsg + sendLen, msgSize - sendLen, 0);
        if (ret > 0) {
            sendLen += ret;
        } else {
            if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            } else {
                fprintf(stderr, "Send failed: %d - %s\n", errno, strerror(errno));
                exit(-1);
            }
        }
    } while (sendLen > 0 && sendLen < msgSize);
    gettimeofday(&after, NULL);

    return  (after.tv_sec - before.tv_sec) * 1000000
            + after.tv_usec - before.tv_usec;
}


int32
_SendServer_Listen(uint16 port) {
    int32               listenFd = -1;
    struct sockaddr_in  serverAddr;

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        fprintf(stderr, "Create socket failed: %d - %s\n", errno, strerror(errno));
        return listenFd;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenFd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) < 0){
        fprintf(stderr, "Bind failed: %d - %s\n", errno, strerror(errno));
        close(listenFd);
        return -1;
    }

    if (listen(listenFd, 10) == -1) {
        fprintf(stderr, "listen socket failed: %d - %s\n", errno, strerror(errno));
        close(listenFd);
        return -1;
    }

    return listenFd;
}


/**
 * API接口库示例程序的主函数
 */
int32
_SendServer_Main(void) {
    int32               listenFd = -1;
    int32               connfd = -1;
    int32               ret = 0;
    char                recvBuff[4096] = {0};

    listenFd = _SendServer_Listen(_port);
    if (listenFd < 0) {
        goto ON_ERROR;
    }

    if (_cpu >= 0) {
        _SendServer_SetCpuAffinity(_cpu);
    }


    do {
        connfd = accept(listenFd, (struct sockaddr*) NULL, NULL);
        if (connfd < 0) {
            fprintf(stderr, "accept failed: %d - %s\n", errno, strerror(errno));
            goto ON_ERROR;
        }

        fprintf(stdout, "New Connection\n");

        do {
            ret = recv(connfd, recvBuff, sizeof(recvBuff), 0);
            if (ret == 0 ) {
                fprintf(stdout, "Client Close\n");
                break;
            } else {
                if (ret < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    fprintf(stdout, "Client recv failed: %d - %s\n",
                            errno, strerror(errno));
                    break;
                }
            }
        } while (1);

        close(connfd);
    } while (1);

    close(listenFd);
    return 0;

ON_ERROR:
    if (connfd > 0) {
        close(connfd);
    }

    if (listenFd > 0) {
        close(listenFd);
    }

    return -1;
}


int
main(int argc, char *argv[]) {
    static const char       short_options[] = "p:c:";
    static struct option    long_options[] = {
        { "port",               1,  NULL,   'p' },
        { "cpu",                1,  NULL,   'c' },
        { 0, 0, 0, 0 }
    };

    int32                   option_index = 0;
    int32                   c = 0;

    optind = 1;
    opterr = 1;
    while ((c = getopt_long(argc, argv, short_options,
            long_options, &option_index)) != EOF) {
        switch (c) {
        case 'p':
            if (optarg) {
                _port = strtol(optarg, NULL, 10);
                if (_port < 1000) {
                    fprintf(stderr, "ERROR: Invalid port value!\n\n");
                    return -EINVAL;
                }
            } else {
                fprintf(stderr, "ERROR: Invalid port params!\n\n");
                return -EINVAL;
            }
            break;

        case 'c':
            if (optarg) {
                _cpu = strtol(optarg, NULL, 10);
                if (_cpu < 0 || _cpu > 15) {
                    fprintf(stderr, "ERROR: Invalid cpu affinity value!\n\n");
                    return -EINVAL;
                }
            } else {
                fprintf(stderr, "ERROR: Invalid cpu affinity params!\n\n");
                return -EINVAL;
            }
            break;

        default:
            fprintf(stderr, "ERROR: Invalid options! ('%c')\n\n", c);
            return -EINVAL;
        }
    }

    return _SendServer_Main();
}
