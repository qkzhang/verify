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
#include    <netinet/tcp.h>


#define USE_IOV      1

#if USE_IOV
#include	<sys/uio.h>
#endif


typedef int64_t         int64;
typedef int32_t         int32;
typedef int16_t         int16;
typedef uint16_t        uint16;


int64           _intervalUs = 0;
int32           _msgCount = 1;
int32           _msgSize = 100;
char            *_pIpAddr = NULL;
uint16          _port = 0;
int16           _cpu = -1;
int32           _block = 0;
int32           _tcpnodelay = 0;
int32           _sndbuf = 0;


static inline int32
_SendClient_GetCpuAffinity(void) {
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
_SendClient_SetCpuAffinity(int32 i) {
#ifdef HAVE_CPU_BIND
    cpu_set_t           cpuset;

    CPU_ZERO(&cpuset);
    CPU_SET(i, &cpuset);

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
_SendClient_Send(int32 socket, const char *pMsg, size_t msgSize) {
    int32               sendLen = 0;
    int32               ret = 0;
    struct timeval      before = {0, 0};
    struct timeval      after = {0, 0};
#if USE_IOV
    struct iovec		iov[2];
    
    iov[0].iov_base = (void *) pMsg;
    iov[0].iov_len = msgSize / 2;
    iov[1].iov_base = (void *) (pMsg + iov[0].iov_len);
    iov[1].iov_len = msgSize - iov[0].iov_len;
#endif

    gettimeofday(&before, NULL);
#if USE_IOV
    do {
		ret = writev(socket, iov, 2);
        if (ret > 0) {
            sendLen += ret;
        } else {
			fprintf(stderr, "writev failed: %d - %s\n", errno, strerror(errno));
			exit(-1);
        }
    } while (sendLen < msgSize);
#else
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
    } while (sendLen < msgSize);
#endif
    gettimeofday(&after, NULL);

    return  (after.tv_sec - before.tv_sec) * 1000000
            + after.tv_usec - before.tv_usec;
}


int32
_SendClient_Connect(const char *pIpAddr, uint16 port) {
    int32               socketFd = -1;
    struct sockaddr_in  serverAddr;

    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        fprintf(stderr, "Create socket failed: %d - %s\n", errno, strerror(errno));
        return socketFd;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, pIpAddr, &serverAddr.sin_addr) <= 0) {
        fprintf(stderr, "inet_pton error for %s\n", pIpAddr);
        close(socketFd);
        return -1;
    }

    if (connect(socketFd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) < 0){
        fprintf(stderr, "Connect failed: %d - %s\n", errno, strerror(errno));
        close(socketFd);
        return -1;
    }



    return socketFd;
}


/**
 * API接口库示例程序的主函数
 */
int32
_SendClient_Main(void) {
    int64               totalLatency = 0;
    int32               msgCount = _msgCount;
    char                *pMsg = malloc(_msgSize);
    int32               socketFd = -1;
    int32               socketFlag = 0;
    int32               iOptVal = 0;
    socklen_t           optLen = sizeof(iOptVal);
    
    if (pMsg == NULL) {
        fprintf(stderr, "malloc failed: %d - %s\n", errno, strerror(errno));
        goto ON_ERROR;
    } else {
        memset(pMsg, 'A', _msgSize);
    }

    socketFd = _SendClient_Connect(_pIpAddr, _port);
    if (socketFd < 0) {
        goto ON_ERROR;
    }

    if (_cpu >= 0) {
        _SendClient_SetCpuAffinity(_cpu);
    }

    if (! _block) {
        socketFlag = fcntl(socketFd, F_GETFL, 0);
        if (fcntl(socketFd, F_SETFL, socketFlag | O_NONBLOCK) < 0) {
            fprintf(stdout, "Set NONBLOCK failed! %d - %s\n", errno, strerror(errno));
        }
    }

    if(0 == getsockopt(socketFd, IPPROTO_TCP, TCP_NODELAY, (char *) &iOptVal, &optLen)) {
        fprintf(stdout, "TCP_NODELAY is %d!\n", iOptVal);
    } else {
        fprintf(stdout, "get TCP_NODELAY failed! %d - %s\n", errno, strerror(errno));
    }

    if (_tcpnodelay && iOptVal == 0) {
        iOptVal = 1;
        if(setsockopt(socketFd, IPPROTO_TCP, TCP_NODELAY, (char *) &iOptVal, optLen) < 0) {
            fprintf(stdout, "setsockopt TCP_NODELAY failed! %d - %s\n", errno, strerror(errno));
        }

        if(0 == getsockopt(socketFd, IPPROTO_TCP, TCP_NODELAY, (char *) &iOptVal, &optLen)) {
            if (iOptVal != 0) {
                fprintf(stdout, "TCP_NODELAY is set successfully! %d!\n", optLen);
            }
        }
    }

    if(0 == getsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, (char *) &iOptVal, &optLen)) {
        fprintf(stdout, "SO_SNDBUF is %d!\n", iOptVal);
    } else {
        fprintf(stdout, "get SO_SNDBUF failed! %d - %s\n", errno, strerror(errno));
    }

    if (_sndbuf > 0) {
        iOptVal = _sndbuf;
        if(setsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, (char *) &iOptVal, optLen) < 0) {
            fprintf(stdout, "setsockopt SO_SNDBUF failed! %d - %s\n", errno, strerror(errno));
        }

        if(0 == getsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, (char *) &iOptVal, &optLen)) {
            fprintf(stdout, "SO_SNDBUF is set successfully! %d!\n", optLen);
        }
    }

    do {
        /* 以 12.67元 购买 浦发银行(600000) 100股 */
        totalLatency += _SendClient_Send(socketFd, pMsg, _msgSize);
        if (_intervalUs > 0) {
            usleep(_intervalUs);
        }
    } while (--msgCount > 0);

    fprintf(stdout, "average cost is: %.02f us\n", (double) totalLatency / _msgCount);


    close(socketFd);
    return 0;

ON_ERROR:
    /* 直接关闭连接, 并释放会话数据 */
    if (socketFd > 0) {
        close(socketFd);
    }

    return -1;
}


int
main(int argc, char *argv[]) {
    static const char       short_options[] = "i:p:m:n:d:c:t:s:b:";
    static struct option    long_options[] = {
        { "ipAddr",             1,  NULL,   'i' },
        { "port",               1,  NULL,   'p' },
        { "msg-size",           1,  NULL,   'm' },
        { "msg-count",          1,  NULL,   'n' },
        { "interval",           1,  NULL,   'd' },
        { "cpu",                1,  NULL,   'c' },
        { "tcp-nodelay",        1,  NULL,   't' },
        { "snd-buf",            1,  NULL,   's' },
        { "block",              1,  NULL,   'b' },
        { 0, 0, 0, 0 }
    };

    int32                   option_index = 0;
    int32                   c = 0;

    optind = 1;
    opterr = 1;
    while ((c = getopt_long(argc, argv, short_options,
            long_options, &option_index)) != EOF) {
        switch (c) {
        case 'i':
            if (optarg) {
                _pIpAddr = optarg;
                if (strlen(_pIpAddr) == 0) {
                    fprintf(stderr, "ERROR: Invalid ip address value!\n\n");
                    return -EINVAL;
                }
            } else {
                fprintf(stderr, "ERROR: Invalid ip address params!\n\n");
                return -EINVAL;
            }
            break;

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

        case 'm':
            if (optarg) {
                _msgSize = strtol(optarg, NULL, 10);
                if (_msgSize < 1) {
                    fprintf(stderr, "ERROR: Invalid msgSize value!\n\n");
                    return -EINVAL;
                }
            } else {
                fprintf(stderr, "ERROR: Invalid msgSize params!\n\n");
                return -EINVAL;
            }
            break;

        case 'n':
            if (optarg) {
                _msgCount = strtol(optarg, NULL, 10);
                if (_msgCount <= 0) {
                    fprintf(stderr, "ERROR: Invalid msg-count value!\n\n");
                    return -EINVAL;
                }
            } else {
                fprintf(stderr, "ERROR: Invalid msg-count params!\n\n");
                return -EINVAL;
            }
            break;

        case 'd':
            if (optarg) {
                _intervalUs = strtol(optarg, NULL, 10);
                if (_intervalUs < 0 || _intervalUs > 100000000) {
                    fprintf(stderr, "ERROR: Invalid interval value!\n\n");
                    return -EINVAL;
                }
            } else {
                fprintf(stderr, "ERROR: Invalid interval params!\n\n");
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

        case 't':
            if (optarg) {
                _tcpnodelay = strtol(optarg, NULL, 10);
            } else {
                fprintf(stderr, "ERROR: Invalid tcp nodelay params!\n\n");
                return -EINVAL;
            }
            break;

        case 's':
            if (optarg) {
                _sndbuf = strtol(optarg, NULL, 10);
                if (_sndbuf <= 0) {
                    fprintf(stderr, "ERROR: Invalid snd-buf value!\n\n");
                    return -EINVAL;
                }
            } else {
                fprintf(stderr, "ERROR: Invalid snd-buf params!\n\n");
                return -EINVAL;
            }
            break;

        case 'b':
            if (optarg) {
                _block = strtol(optarg, NULL, 10);
            } else {
                fprintf(stderr, "ERROR: Invalid block params!\n\n");
                return -EINVAL;
            }
            break;

        default:
            fprintf(stderr, "ERROR: Invalid options! ('%c')\n\n", c);
            return -EINVAL;
        }
    }

    return _SendClient_Main();
}
