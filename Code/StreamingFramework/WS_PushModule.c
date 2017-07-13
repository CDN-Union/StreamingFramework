#include "WS_PushModule.h"
#include "pt_module.h"
#include <sys/select.h>
#include "log.h"
#include "rtmp_sys.h"
#include "time.h"
#include "rtmp.h"
#include <string.h>
#include "amf.h"
#define RTMP_LARGE_HEADER_SIZE 12

void WS_WMP_close(PILI_RTMP *r);
int  WS_WMP_RTMP_Connect(PILI_RTMP *r, PILI_RTMPPacket *cp, RTMPError *error);
int  WS_WMP_ConnectStream(PILI_RTMP *r, int seekTime, RTMPError *error);
int  WS_WMP_SendConnectPacket(PILI_RTMP *r, PILI_RTMPPacket *cp, RTMPError *error);
int  WS_WMP_RTMP_ReadPacket(PILI_RTMP *r, PILI_RTMPPacket *packet);
int  WS_WMP_RTMP_ClientPacket(PILI_RTMP *r, PILI_RTMPPacket *packet) ;
int  WS_WMP_SendFCPublish(PILI_RTMP *r, RTMPError *error) ;
int  WS_WMP_RTMP_SendPacket(PILI_RTMP *r, PILI_RTMPPacket *packet, int queue, RTMPError *error);
void WS_WMP_PILI_DecodeTEA(PILI_AVal *key, PILI_AVal *text) ;

#define SAVC(x) static const PILI_AVal av_##x = AVC(#x)
SAVC(app);
SAVC(connect);
SAVC(flashVer);
SAVC(swfUrl);
SAVC(pageUrl);
SAVC(tcUrl);
SAVC(fpad);
SAVC(capabilities);
SAVC(audioCodecs);
SAVC(videoCodecs);
SAVC(videoFunction);
SAVC(objectEncoding);
SAVC(secureToken);
SAVC(secureTokenResponse);
SAVC(type);
SAVC(nonprivate);
SAVC(xreqid);
SAVC(negotiate);
SAVC(FCPublish);
SAVC(publish);
SAVC(live);
SAVC(releaseStream);

static const int packetSize[] = {12, 8, 4, 1};

#define HEX2BIN(a) (((a)&0x40) ? ((a)&0xf) + 9 : ((a)&0xf))


static int WS_WMP_SendReleaseStream(PILI_RTMP *r, RTMPError *error) {
    PILI_RTMPPacket packet;
    char pbuf[1024], *pend = pbuf + sizeof(pbuf);
    char *enc;
    
    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet.m_packetType = 0x14; /* INVOKE */
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;
    
    enc = packet.m_body;
    enc = PILI_AMF_EncodeString(enc, pend, &av_releaseStream);
    enc = PILI_AMF_EncodeNumber(enc, pend, ++r->m_numInvokes);
    *enc++ = PILI_AMF_NULL;
    enc = PILI_AMF_EncodeString(enc, pend, &r->Link.playpath);
    if (!enc)
        return FALSE;
    
    packet.m_nBodySize = enc - packet.m_body;
    
    return WS_WMP_RTMP_SendPacket(r, &packet, FALSE, error);;
}

 void WS_WMP_PILI_DecodeTEA(PILI_AVal *key, PILI_AVal *text) {
    uint32_t *v, k[4] = {0}, u;
    uint32_t z, y, sum = 0, e, DELTA = 0x9e3779b9;
    int32_t p, q;
    int i, n;
    unsigned char *ptr, *out;
    
    /* prep key: pack 1st 16 chars into 4 LittleEndian ints */
    ptr = (unsigned char *)key->av_val;
    u = 0;
    n = 0;
    v = k;
    p = key->av_len > 16 ? 16 : key->av_len;
    for (i = 0; i < p; i++) {
        u |= ptr[i] << (n * 8);
        if (n == 3) {
            *v++ = u;
            u = 0;
            n = 0;
        } else {
            n++;
        }
    }
    /* any trailing chars */
    if (u)
        *v = u;
    
    /* prep text: hex2bin, multiples of 4 */
    n = (text->av_len + 7) / 8;
    out = malloc(n * 8);
    ptr = (unsigned char *)text->av_val;
    v = (uint32_t *)out;
    for (i = 0; i < n; i++) {
        u = (HEX2BIN(ptr[0]) << 4) + HEX2BIN(ptr[1]);
        u |= ((HEX2BIN(ptr[2]) << 4) + HEX2BIN(ptr[3])) << 8;
        u |= ((HEX2BIN(ptr[4]) << 4) + HEX2BIN(ptr[5])) << 16;
        u |= ((HEX2BIN(ptr[6]) << 4) + HEX2BIN(ptr[7])) << 24;
        *v++ = u;
        ptr += 8;
    }
    v = (uint32_t *)out;
    
    /* http://www.movable-type.co.uk/scripts/tea-block.html */
#define MX (((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + (k[(p & 3) ^ e] ^ z));
    z = v[n - 1];
    y = v[0];
    q = 6 + 52 / n;
    sum = q * DELTA;
    while (sum != 0) {
        e = sum >> 2 & 3;
        for (p = n - 1; p > 0; p--)
            z = v[p - 1], y = v[p] -= MX;
        z = v[n - 1];
        y = v[0] -= MX;
        sum -= DELTA;
    }
    
    text->av_len /= 2;
    memcpy(text->av_val, out, text->av_len);
    free(out);
}
int WS_WMP_SendFCPublish(PILI_RTMP *r, RTMPError *error) {
    PILI_RTMPPacket packet;
    char pbuf[1024], *pend = pbuf + sizeof(pbuf);
    char *enc;
    
    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet.m_packetType = 0x14; /* INVOKE */
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;
    
    enc = packet.m_body;
    enc = PILI_AMF_EncodeString(enc, pend, &av_FCPublish);
    enc = PILI_AMF_EncodeNumber(enc, pend, ++r->m_numInvokes);
    *enc++ = PILI_AMF_NULL;
    enc = PILI_AMF_EncodeString(enc, pend, &r->Link.playpath);
    if (!enc)
        return FALSE;
    
    packet.m_nBodySize = enc - packet.m_body;
    
    return WS_WMP_RTMP_SendPacket(r, &packet, FALSE, error);
}
static int WS_WMP_SendPublish(PILI_RTMP *r, RTMPError *error) {
    PILI_RTMPPacket packet;
    char pbuf[1024], *pend = pbuf + sizeof(pbuf);
    char *enc;
    
    packet.m_nChannel = 0x04; /* source channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = 0x14; /* INVOKE */
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = r->m_stream_id;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;
    
    enc = packet.m_body;
    enc = PILI_AMF_EncodeString(enc, pend, &av_publish);
    enc = PILI_AMF_EncodeNumber(enc, pend, ++r->m_numInvokes);
    *enc++ = PILI_AMF_NULL;
    enc = PILI_AMF_EncodeString(enc, pend, &r->Link.playpath);
    if (!enc)
        return FALSE;
    
    /* FIXME: should we choose live based on Link.lFlags & RTMP_LF_LIVE? */
    enc = PILI_AMF_EncodeString(enc, pend, &av_live);
    if (!enc)
        return FALSE;
    
    packet.m_nBodySize = enc - packet.m_body;
    
    return WS_WMP_RTMP_SendPacket(r, &packet, TRUE, error);
}
static int WS_WMP_ReadN(PILI_RTMP *r, char *buffer, int n) {
    
    char* ptr = buffer;
    int nOriginalSize = n;
    int sb_socket = *(int*) r->m_userData;
    while (n > 0) {
        int nBytes = 0;
        nBytes = pt_socket_recv(sb_socket,ptr,n);
        if (nBytes < 0) {
            return -1;
        }
        else if(nBytes == 0)
        {
            return 0;
        }
        n -= nBytes;
        ptr += nBytes;
    }
    
    return nOriginalSize;
}

static int WS_WMP_WriteN(PILI_RTMP *r, const char *buffer, int n, RTMPError *error) {

    int sb_socket = *(int*) r->m_userData;
    int nBytes = pt_socket_send(sb_socket,buffer,n);
    if (nBytes <= 0) {
        return 0;
    }
    return 1;
}


void WS_WMP_close(PILI_RTMP *r)
{
    int sb_socket = *(int*) r->m_userData;
    if (sb_socket != -1) {
        int *psb_socket = (int*) r->m_userData;
         pt_socket_close(sb_socket);
        *(psb_socket)= -1;
    }
}
    
static int WS_WMP_HandShake(PILI_RTMP *r, int FP9HandShake, RTMPError *error) {

    const int RTMP_SIG_SIZE = 1536;
    int i;
    uint32_t uptime, suptime;
    int bMatch;
    char type;
    char clientbuf[RTMP_SIG_SIZE + 1], *clientsig = clientbuf + 1;
    char serversig[RTMP_SIG_SIZE];
    
    clientbuf[0] = 0x03; /* not encrypted */
    
    uptime = htonl(PILI_RTMP_GetTime());
    memcpy(clientsig, &uptime, 4);
    
    memset(&clientsig[4], 0, 4);
    
#ifdef _DEBUG
    for (i = 8; i < RTMP_SIG_SIZE; i++)
        clientsig[i] = 0xff;
#else
    for (i = 8; i < RTMP_SIG_SIZE; i++)
        clientsig[i] = (char)(rand() % 256);
#endif
    
    if (!WS_WMP_WriteN(r, clientbuf, RTMP_SIG_SIZE + 1, error))
        return FALSE;
    
    if (WS_WMP_ReadN(r, &type, 1) != 1) /* 0x03 or 0x06 */
        return FALSE;
    
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s: Type Answer   : %02X", __FUNCTION__, type);
    
    if (type != clientbuf[0])
        PILI_RTMP_Log(PILI_RTMP_LOGWARNING, "%s: Type mismatch: client sent %d, server answered %d",
                      __FUNCTION__, clientbuf[0], type);
    
    if (WS_WMP_ReadN(r, serversig, RTMP_SIG_SIZE) != RTMP_SIG_SIZE)
        return FALSE;
    
    /* decode server response */
    
    memcpy(&suptime, serversig, 4);
    suptime = ntohl(suptime);
    
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s: Server Uptime : %d", __FUNCTION__, suptime);
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s: FMS Version   : %d.%d.%d.%d", __FUNCTION__,
                  serversig[4], serversig[5], serversig[6], serversig[7]);
    
    /* 2nd part of handshake */
    if (!WS_WMP_WriteN(r, serversig, RTMP_SIG_SIZE, error))
        return FALSE;
    
    if (WS_WMP_ReadN(r, serversig, RTMP_SIG_SIZE) != RTMP_SIG_SIZE)
        return FALSE;
    
    bMatch = (memcmp(serversig, clientsig, RTMP_SIG_SIZE) == 0);
    if (!bMatch) {
        PILI_RTMP_Log(PILI_RTMP_LOGWARNING, "%s, client signature does not match!", __FUNCTION__);
    }
    return TRUE;
}

static int WS_add_addr_info(PILI_RTMP *r, struct addrinfo *hints, struct addrinfo **ai, PILI_AVal *host, int port, RTMPError *error)
{
    char *hostname;
    int ret = TRUE;
    if (host->av_val[host->av_len]) {
        hostname = malloc(host->av_len + 1);
        memcpy(hostname, host->av_val, host->av_len);
        hostname[host->av_len] = '\0';
    } else {
        hostname = host->av_val;
    }
    
    struct addrinfo *cur_ai;
    char portstr[10];
    snprintf(portstr, sizeof(portstr), "%d", port);
    int addrret = getaddrinfo(hostname, portstr, hints, ai);
    if (addrret != 0) {
        char msg[100];
        memset(msg, 0, 100);
        strcat(msg, "Problem accessing the DNS. addr: ");
        strcat(msg, hostname);
        
        PILI_RTMPError_Alloc(error, strlen(msg));
        error->code = PILI_RTMPErrorAccessDNSFailed;
        strcpy(error->message, msg);
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "Problem accessing the DNS. %d (addr: %s) (port: %s)", addrret, hostname, portstr);
        ret = FALSE;
    }else{
        
        if(((struct addrinfo *)*ai)->ai_family == AF_INET6){
            struct sockaddr_in6 * addrIn6;
            addrIn6 = (struct sockaddr_in6 *)((struct addrinfo *)*ai)->ai_addr;
            char ipbuf[48];
            inet_ntop(AF_INET6,&addrIn6->sin6_addr, ipbuf, sizeof(ipbuf));
            
        }else{
            struct sockaddr_in *addr;
            addr = (struct sockaddr_in *)((struct addrinfo *)*ai)->ai_addr;
            char ipbuf[16];
            inet_ntop(AF_INET,&addr->sin_addr, ipbuf, sizeof(ipbuf));
            
        }
        
    }
    
    if (hostname != host->av_val) {
        free(hostname);
    }
    return ret;

    
}



int WS_WMP_RTMP_Connect0(PILI_RTMP *r, struct addrinfo *ai, unsigned short port, RTMPError *error) {
    int* psb_socket = (int*)r->m_userData;
    r->m_sb.sb_timedout = FALSE;
    r->m_pausing = 0;
    r->m_fDuration = 0.0;
    
    *psb_socket = pt_socket_new();
    if( pt_socket_connect(*psb_socket,ai->ai_addr, ai->ai_addrlen) < 0)
    {
        return FALSE;
    }
    return TRUE;
}


int WS_WMP_RTMP_Connect1(PILI_RTMP *r, PILI_RTMPPacket *cp, RTMPError *error) {
    if (r->Link.protocol & RTMP_FEATURE_SSL) {
#if defined(CRYPTO) && !defined(NO_SSL)
        TLS_client(RTMP_TLS_ctx, r->m_sb.sb_ssl);
        TLS_setfd(r->m_sb.sb_ssl, r->m_sb.sb_socket);
        if (TLS_connect(r->m_sb.sb_ssl) < 0) {
            if (error) {
                char msg[100];
                memset(msg, 0, 100);
                strcat(msg, "TLS_Connect failed.");
                PILI_RTMPError_Alloc(error, strlen(msg));
                error->code = PILI_RTMPErrorTLSConnectFailed;
                strcpy(error->message, msg);
            }
            
            PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, TLS_Connect failed", __FUNCTION__);
            WS_WMP_close(r);
            return FALSE;
        }
#else
        if (error) {
            char msg[100];
            memset(msg, 0, 100);
            strcat(msg, "No SSL/TLS support.");
            PILI_RTMPError_Alloc(error, strlen(msg));
            error->code = PILI_RTMPErrorNoSSLOrTLSSupport;
            strcpy(error->message, msg);
        }
        
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, no SSL/TLS support", __FUNCTION__);
        WS_WMP_close(r);
        return FALSE;
        
#endif
    }
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, ... connected, handshaking", __FUNCTION__);
    if (!WS_WMP_HandShake(r, TRUE, error)) {
        if (error) {
            char msg[100];
            memset(msg, 0, 100);
            strcat(msg, "Handshake failed.");
            PILI_RTMPError_Alloc(error, strlen(msg));
            error->code = PILI_RTMPErrorHandshakeFailed;
            strcpy(error->message, msg);
        }
        
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, handshake failed.", __FUNCTION__);
        WS_WMP_close(r);
        return FALSE;
    }
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, handshaked", __FUNCTION__);
    
    if (!WS_WMP_SendConnectPacket(r, cp, error)) {
        if (error) {
            char msg[100];
            memset(msg, 0, 100);
            strcat(msg, "PILI_RTMP connect failed.");
            PILI_RTMPError_Alloc(error, strlen(msg));
            error->code = PILI_RTMPErrorRTMPConnectFailed;
            strcpy(error->message, msg);
        }
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, PILI_RTMP connect failed.", __FUNCTION__);
        WS_WMP_close(r);
        return FALSE;
    }
    return TRUE;
}

int WS_WMP_RTMP_Connect(PILI_RTMP *r, PILI_RTMPPacket *cp, RTMPError *error) {
    //获取hub
    char hub[5] = {0};
    
    if (r->Link.app.av_len>4) {
        strncpy(hub, r->Link.app.av_val,4);
    }else if(r->Link.app.av_len>0){
        strncpy(hub, r->Link.app.av_val,r->Link.app.av_len);
    }
    

    struct PILI_CONNECTION_TIME conn_time;
    if (!r->Link.hostname.av_len)
        return FALSE;
    
    struct addrinfo hints = {0}, *ai, *cur_ai;
    
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    unsigned short port;
    if (r->Link.socksport) {
        port = r->Link.socksport;
        /* Connect via SOCKS */
        if (!WS_add_addr_info(r, &hints, &ai, &r->Link.sockshost, r->Link.socksport, error)) {
            return FALSE;
        }
    } else {
        port = r->Link.port;
        /* Connect directly */
        if (!WS_add_addr_info(r, &hints, &ai, &r->Link.hostname, r->Link.port, error)) {
            return FALSE;
        }
    }
    r->ip = 0; //useless for ipv6
    cur_ai = ai;
    
    int t1 = PILI_RTMP_GetTime();
    if (!WS_WMP_RTMP_Connect0(r, cur_ai, port, error)) {
        freeaddrinfo(ai);
        return FALSE;
    }
    conn_time.connect_time = PILI_RTMP_GetTime() - t1;
    r->m_bSendCounter = TRUE;
    
    int t2 = PILI_RTMP_GetTime();
    int ret = WS_WMP_RTMP_Connect1(r, cp, error);
    conn_time.handshake_time = PILI_RTMP_GetTime() - t2;
    
    freeaddrinfo(ai);
    return ret;
}
SAVC(_checkbw);

static int  WS_WMP_SendCheckBW(PILI_RTMP *r, RTMPError *error) {
    PILI_RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;
    
    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = 0x14; /* INVOKE */
    packet.m_nTimeStamp = 0; /* RTMP_GetTime(); */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;
    
    enc = packet.m_body;
    enc = PILI_AMF_EncodeString(enc, pend, &av__checkbw);
    enc = PILI_AMF_EncodeNumber(enc, pend, ++r->m_numInvokes);
    *enc++ = PILI_AMF_NULL;
    
    packet.m_nBodySize = enc - packet.m_body;
    
    /* triggers _onbwcheck and eventually results in _onbwdone */
    return WS_WMP_RTMP_SendPacket(r, &packet, FALSE, error);
}

int WS_RTMP_Connect1(PILI_RTMP *r, PILI_RTMPPacket *cp, RTMPError *error) {
    if (r->Link.protocol & RTMP_FEATURE_SSL) {
#if defined(CRYPTO) && !defined(NO_SSL)
        TLS_client(RTMP_TLS_ctx, r->m_sb.sb_ssl);
        TLS_setfd(r->m_sb.sb_ssl, r->m_sb.sb_socket);
        if (TLS_connect(r->m_sb.sb_ssl) < 0) {
            if (error) {
                char msg[100];
                memset(msg, 0, 100);
                strcat(msg, "TLS_Connect failed.");
                PILI_RTMPError_Alloc(error, strlen(msg));
                error->code = PILI_RTMPErrorTLSConnectFailed;
                strcpy(error->message, msg);
            }
            
            PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, TLS_Connect failed", __FUNCTION__);
            WS_WMP_close(r);
            return FALSE;
        }
#else
        if (error) {
            char msg[100];
            memset(msg, 0, 100);
            strcat(msg, "No SSL/TLS support.");
            PILI_RTMPError_Alloc(error, strlen(msg));
            error->code = PILI_RTMPErrorNoSSLOrTLSSupport;
            strcpy(error->message, msg);
        }
        
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, no SSL/TLS support", __FUNCTION__);
        WS_WMP_close(r);
        return FALSE;
        
#endif
    }
    
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, ... connected, handshaking", __FUNCTION__);
    if (!WS_WMP_HandShake(r, TRUE, error)) {
        if (error) {
            char msg[100];
            memset(msg, 0, 100);
            strcat(msg, "Handshake failed.");
            PILI_RTMPError_Alloc(error, strlen(msg));
            error->code = PILI_RTMPErrorHandshakeFailed;
            strcpy(error->message, msg);
        }
        
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, handshake failed.", __FUNCTION__);
        WS_WMP_close(r);
        return FALSE;
    }
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, handshaked", __FUNCTION__);
    
    if (!WS_WMP_SendConnectPacket(r, cp, error)) {
        if (error) {
            char msg[100];
            memset(msg, 0, 100);
            strcat(msg, "PILI_RTMP connect failed.");
            PILI_RTMPError_Alloc(error, strlen(msg));
            error->code = PILI_RTMPErrorRTMPConnectFailed;
            strcpy(error->message, msg);
        }
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, PILI_RTMP connect failed.", __FUNCTION__);
        WS_WMP_close(r);
        return FALSE;
    }
    return TRUE;
}



 int WS_WMP_SendConnectPacket(PILI_RTMP *r, PILI_RTMPPacket *cp, RTMPError *error) {
    PILI_RTMPPacket packet;
    char pbuf[4096], *pend = pbuf + sizeof(pbuf);
    char *enc;
    
    if (cp)
        return WS_WMP_RTMP_SendPacket(r, cp, TRUE, error);
    
    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = 0x14; /* INVOKE */
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;
    
    enc = packet.m_body;
    enc = PILI_AMF_EncodeString(enc, pend, &av_connect);
    enc = PILI_AMF_EncodeNumber(enc, pend, ++r->m_numInvokes);
    *enc++ = PILI_AMF_OBJECT;
    
    enc = PILI_AMF_EncodeNamedString(enc, pend, &av_app, &r->Link.app);
    if (!enc)
        return FALSE;
    
    if (r->Link.protocol & RTMP_FEATURE_WRITE) {
        enc = PILI_AMF_EncodeNamedString(enc, pend, &av_type, &av_nonprivate);
        if (!enc)
            return FALSE;
    }
    if (r->Link.flashVer.av_len) {
        enc = PILI_AMF_EncodeNamedString(enc, pend, &av_flashVer, &r->Link.flashVer);
        if (!enc)
            return FALSE;
    }
    if (r->Link.swfUrl.av_len) {
        enc = PILI_AMF_EncodeNamedString(enc, pend, &av_swfUrl, &r->Link.swfUrl);
        if (!enc)
            return FALSE;
    }
    if (r->Link.tcUrl.av_len) {
        enc = PILI_AMF_EncodeNamedString(enc, pend, &av_tcUrl, &r->Link.tcUrl);
        if (!enc)
            return FALSE;
    }
    if (r->Link.negotiate.av_len) {
        enc = PILI_AMF_EncodeNamedString(enc, pend, &av_negotiate, &r->Link.negotiate);
        if (!enc)
            return FALSE;
    }
    if (!(r->Link.protocol & RTMP_FEATURE_WRITE)) {
        enc = PILI_AMF_EncodeNamedBoolean(enc, pend, &av_fpad, FALSE);
        if (!enc)
            return FALSE;
        enc = PILI_AMF_EncodeNamedNumber(enc, pend, &av_capabilities, 15.0);
        if (!enc)
            return FALSE;
        enc = PILI_AMF_EncodeNamedNumber(enc, pend, &av_audioCodecs, r->m_fAudioCodecs);
        if (!enc)
            return FALSE;
        enc = PILI_AMF_EncodeNamedNumber(enc, pend, &av_videoCodecs, r->m_fVideoCodecs);
        if (!enc)
            return FALSE;
        enc = PILI_AMF_EncodeNamedNumber(enc, pend, &av_videoFunction, 1.0);
        if (!enc)
            return FALSE;
        if (r->Link.pageUrl.av_len) {
            enc = PILI_AMF_EncodeNamedString(enc, pend, &av_pageUrl, &r->Link.pageUrl);
            if (!enc)
                return FALSE;
        }
    }
    if (r->m_fEncoding != 0.0 || r->m_bSendEncoding) { /* AMF0, AMF3 not fully supported yet */
        enc = PILI_AMF_EncodeNamedNumber(enc, pend, &av_objectEncoding, r->m_fEncoding);
        if (!enc)
            return FALSE;
    }
    if (enc + 3 >= pend)
        return FALSE;
    *enc++ = 0;
    *enc++ = 0; /* end of object - 0x00 0x00 0x09 */
    *enc++ = PILI_AMF_OBJECT_END;
    
    /* add auth string */
    if (r->Link.auth.av_len) {
        enc = PILI_AMF_EncodeBoolean(enc, pend, r->Link.lFlags & RTMP_LF_AUTH);
        if (!enc)
            return FALSE;
        enc = PILI_AMF_EncodeString(enc, pend, &r->Link.auth);
        if (!enc)
            return FALSE;
    }
    if (r->Link.extras.o_num) {
        int i;
        for (i = 0; i < r->Link.extras.o_num; i++) {
            enc = PILI_AMFProp_Encode(&r->Link.extras.o_props[i], enc, pend);
            if (!enc)
                return FALSE;
        }
    }
    packet.m_nBodySize = enc - packet.m_body;
    
    return WS_WMP_RTMP_SendPacket(r, &packet, TRUE, error);
}
int WS_WMP_ConnectStream(PILI_RTMP *r, int seekTime, RTMPError *error) {
    PILI_RTMPPacket packet = {0};
    /* seekTime was already set by SetupStream / SetupURL.
     * This is only needed by ReconnectStream.
     */
    if (seekTime > 0)
        r->Link.seekTime = seekTime;
    
    r->m_mediaChannel = 0;
    
    while (!r->m_bPlaying  && WS_WMP_RTMP_ReadPacket(r, &packet)) {
        if (RTMPPacket_IsReady(&packet)) {
            if (!packet.m_nBodySize)
                continue;
            if ((packet.m_packetType == RTMP_PACKET_TYPE_AUDIO) ||
                (packet.m_packetType == RTMP_PACKET_TYPE_VIDEO) ||
                (packet.m_packetType == RTMP_PACKET_TYPE_INFO)) {
                PILI_RTMP_Log(PILI_RTMP_LOGWARNING, "Received FLV packet before play()! Ignoring.");
                PILI_RTMPPacket_Free(&packet);
                continue;
            }
            WS_WMP_RTMP_ClientPacket(r, &packet);
            PILI_RTMPPacket_Free(&packet);
        }
    }
    
    return r->m_bPlaying;
}

static int WS_WMP_DecodeInt32LE(const char *data) {
    unsigned char *c = (unsigned char *)data;
    unsigned int val;
    
    val = (c[3] << 24) | (c[2] << 16) | (c[1] << 8) | c[0];
    return val;
}
int WS_WMP_RTMP_ReadPacket(PILI_RTMP *r, PILI_RTMPPacket *packet) {
    uint8_t hbuf[RTMP_MAX_HEADER_SIZE] = {0};
    char *header = (char *)hbuf;
    int nSize, hSize, nToRead, nChunk;
    int didAlloc = FALSE;
    
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG2, "%s: fd=%d", __FUNCTION__, r->m_sb.sb_socket);
    
    if (WS_WMP_ReadN(r, (char *)hbuf, 1) == 0) {
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, failed to read PILI_RTMP packet header", __FUNCTION__);
        return FALSE;
    }
    
    packet->m_headerType = (hbuf[0] & 0xc0) >> 6;
    packet->m_nChannel = (hbuf[0] & 0x3f);
    header++;
    if (packet->m_nChannel == 0) {
        if (WS_WMP_ReadN(r, (char *)&hbuf[1], 1) != 1) {
            PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, failed to read PILI_RTMP packet header 2nd byte",
                          __FUNCTION__);
            return FALSE;
        }
        packet->m_nChannel = hbuf[1];
        packet->m_nChannel += 64;
        header++;
    } else if (packet->m_nChannel == 1) {
        int tmp;
        if (WS_WMP_ReadN(r, (char *)&hbuf[1], 2) != 2) {
            PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, failed to read PILI_RTMP packet header 3nd byte",
                          __FUNCTION__);
            return FALSE;
        }
        tmp = (hbuf[2] << 8) + hbuf[1];
        packet->m_nChannel = tmp + 64;
        PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, m_nChannel: %0x", __FUNCTION__, packet->m_nChannel);
        header += 2;
    }
    
    nSize = packetSize[packet->m_headerType];
    
    if (nSize == RTMP_LARGE_HEADER_SIZE) /* if we get a full header the timestamp is absolute */
        packet->m_hasAbsTimestamp = TRUE;
    
    else if (nSize < RTMP_LARGE_HEADER_SIZE) { /* using values from the last message of this channel */
        if (r->m_vecChannelsIn[packet->m_nChannel])
            memcpy(packet, r->m_vecChannelsIn[packet->m_nChannel],
                   sizeof(PILI_RTMPPacket));
    }
    
    nSize--;
    
    if (nSize > 0 && WS_WMP_ReadN(r, header, nSize) != nSize) {
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, failed to read PILI_RTMP packet header. type: %x",
                      __FUNCTION__, (unsigned int)hbuf[0]);
        return FALSE;
    }
    
    hSize = nSize + (header - (char *)hbuf);
    
    if (nSize >= 3) {
        packet->m_nTimeStamp = PILI_AMF_DecodeInt24(header);
        packet->m_useExtTimestamp = FALSE;
        
        /*PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, reading PILI_RTMP packet chunk on channel %x, headersz %i, timestamp %i, abs timestamp %i", __FUNCTION__, packet.m_nChannel, nSize, packet.m_nTimeStamp, packet.m_hasAbsTimestamp); */
        
        if (nSize >= 6) {
            packet->m_nBodySize = PILI_AMF_DecodeInt24(header + 3);
            packet->m_nBytesRead = 0;
            PILI_RTMPPacket_Free(packet);
            
            if (nSize > 6) {
                packet->m_packetType = header[6];
                
                if (nSize == 11)
                    packet->m_nInfoField2 = WS_WMP_DecodeInt32LE(header + 7);
            }
        }
        if (packet->m_nTimeStamp == 0xffffff) {
            if (WS_WMP_ReadN(r, header + nSize, 4) != 4) {
                PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, failed to read extended timestamp",
                              __FUNCTION__);
                return FALSE;
            }
            packet->m_nTimeStamp = PILI_AMF_DecodeInt32(header + nSize);
            packet->m_useExtTimestamp = TRUE;
            hSize += 4;
        }
    } else if (packet->m_nTimeStamp >= 0xffffff){
        if (WS_WMP_ReadN(r, header + nSize, 4) != 4) {
            PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, failed to read extended timestamp",
                          __FUNCTION__);
            return FALSE;
        }
        packet->m_nTimeStamp = PILI_AMF_DecodeInt32(header + nSize);
        hSize += 4;
    }
    
    PILI_RTMP_LogHexString(PILI_RTMP_LOGDEBUG2, (uint8_t *)hbuf, hSize);
    
    if (packet->m_nBodySize > 0 && packet->m_body == NULL) {
        if (!PILI_RTMPPacket_Alloc(packet, packet->m_nBodySize)) {
            PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, failed to allocate packet", __FUNCTION__);
            return FALSE;
        }
        didAlloc = TRUE;
        packet->m_headerType = (hbuf[0] & 0xc0) >> 6;
    }
    
    nToRead = packet->m_nBodySize - packet->m_nBytesRead;
    nChunk = r->m_inChunkSize;
    if (nToRead < nChunk)
        nChunk = nToRead;
    
    /* Does the caller want the raw chunk? */
    if (packet->m_chunk) {
        packet->m_chunk->c_headerSize = hSize;
        memcpy(packet->m_chunk->c_header, hbuf, hSize);
        packet->m_chunk->c_chunk = packet->m_body + packet->m_nBytesRead;
        packet->m_chunk->c_chunkSize = nChunk;
    }
    
    if (WS_WMP_ReadN(r, packet->m_body + packet->m_nBytesRead, nChunk) != nChunk) {
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, failed to read PILI_RTMP packet body. len: %lu",
                      __FUNCTION__, packet->m_nBodySize);
        return FALSE;
    }
    
    PILI_RTMP_LogHexString(PILI_RTMP_LOGDEBUG2, (uint8_t *)packet->m_body + packet->m_nBytesRead, nChunk);
    
    packet->m_nBytesRead += nChunk;
    
    /* keep the packet as ref for other packets on this channel */
    if (!r->m_vecChannelsIn[packet->m_nChannel])
        r->m_vecChannelsIn[packet->m_nChannel] = malloc(sizeof(PILI_RTMPPacket));
    memcpy(r->m_vecChannelsIn[packet->m_nChannel], packet, sizeof(PILI_RTMPPacket));
    
    if (RTMPPacket_IsReady(packet)) {
        /* make packet's timestamp absolute */
        if (!packet->m_hasAbsTimestamp)
            packet->m_nTimeStamp += r->m_channelTimestamp[packet->m_nChannel]; /* timestamps seem to be always relative!! */
        
        r->m_channelTimestamp[packet->m_nChannel] = packet->m_nTimeStamp;
        
        /* reset the data from the stored packet. we keep the header since we may use it later if a new packet for this channel */
        /* arrives and requests to re-use some info (small packet header) */
        r->m_vecChannelsIn[packet->m_nChannel]->m_body = NULL;
        r->m_vecChannelsIn[packet->m_nChannel]->m_nBytesRead = 0;
        r->m_vecChannelsIn[packet->m_nChannel]->m_hasAbsTimestamp = FALSE; /* can only be false if we reuse header */
    } else {
        packet->m_body = NULL; /* so it won't be erased on free */
    }
    
    return TRUE;
}
static void WS_WMP_AV_queue(PILI_RTMP_METHOD **vals, int *num, PILI_AVal *av, int txn) {
    char *tmp;
    if (!(*num & 0x0f))
        *vals = realloc(*vals, (*num + 16) * sizeof(PILI_RTMP_METHOD));
    tmp = malloc(av->av_len + 1);
    memcpy(tmp, av->av_val, av->av_len);
    tmp[av->av_len] = '\0';
    (*vals)[*num].num = txn;
    (*vals)[*num].name.av_len = av->av_len;
    (*vals)[(*num)++].name.av_val = tmp;
}
static void WS_WMP_AV_erase(PILI_RTMP_METHOD *vals, int *num, int i, int freeit) {
    if (freeit)
        free(vals[i].name.av_val);
    (*num)--;
    for (; i < *num; i++) {
        vals[i] = vals[i + 1];
    }
    vals[i].name.av_val = NULL;
    vals[i].name.av_len = 0;
    vals[i].num = 0;
}
static int WS_WMP_EncodeInt32LE(char *output, int nVal) {
    output[0] = nVal;
    nVal >>= 8;
    output[1] = nVal;
    nVal >>= 8;
    output[2] = nVal;
    nVal >>= 8;
    output[3] = nVal;
    return 4;
}
static void WS_WMP_HandleChangeChunkSize(PILI_RTMP *r, const PILI_RTMPPacket *packet) {
    if (packet->m_nBodySize >= 4) {
        r->m_inChunkSize = PILI_AMF_DecodeInt32(packet->m_body);
        PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, received: chunk size change to %d", __FUNCTION__,
                      r->m_inChunkSize);
    }
}

static void WS_WMP_HandleServerBW(PILI_RTMP *r, const PILI_RTMPPacket *packet) {
    r->m_nServerBW = PILI_AMF_DecodeInt32(packet->m_body);
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s: server BW = %d", __FUNCTION__, r->m_nServerBW);
}

static void WS_WMP_HandleClientBW(PILI_RTMP *r, const PILI_RTMPPacket *packet) {
    r->m_nClientBW = PILI_AMF_DecodeInt32(packet->m_body);
    if (packet->m_nBodySize > 4)
        r->m_nClientBW2 = packet->m_body[4];
    else
        r->m_nClientBW2 = -1;
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s: client BW = %d %d", __FUNCTION__, r->m_nClientBW,
                  r->m_nClientBW2);
}
#define AVMATCH(a1, a2)              \
((a1)->av_len == (a2)->av_len && \
!memcmp((a1)->av_val, (a2)->av_val, (a1)->av_len))
SAVC(_result);
SAVC(createStream);
SAVC(code);
SAVC(level);
SAVC(onStatus);
SAVC(ping);
SAVC(onBWDone);

SAVC(FCSubscribe);
int WS_WMP_RTMP_SendCreateStream(PILI_RTMP *r, RTMPError *error) {
    PILI_RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;
    
    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet.m_packetType = 0x14; /* INVOKE */
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;
    
    enc = packet.m_body;
    enc = PILI_AMF_EncodeString(enc, pend, &av_createStream);
    enc = PILI_AMF_EncodeNumber(enc, pend, ++r->m_numInvokes);
    *enc++ = PILI_AMF_NULL; /* NULL */
    
    packet.m_nBodySize = enc - packet.m_body;
    
    return WS_WMP_RTMP_SendPacket(r, &packet, TRUE, error);
}
SAVC(_onbwcheck);
SAVC(pong);
SAVC(_onbwdone);


 int WS_WMP_SendPong(PILI_RTMP *r, double txn, RTMPError *error) {
    PILI_RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);
    char *enc;
    
    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet.m_packetType = 0x14; /* INVOKE */
    packet.m_nTimeStamp = 0x16 * r->m_nBWCheckCounter; /* temp inc value. till we figure it out. */
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;
    
    enc = packet.m_body;
    enc = PILI_AMF_EncodeString(enc, pend, &av_pong);
    enc = PILI_AMF_EncodeNumber(enc, pend, txn);
    *enc++ = PILI_AMF_NULL;
    
    packet.m_nBodySize = enc - packet.m_body;
    
    return WS_WMP_RTMP_SendPacket(r, &packet, FALSE, error);
}


 int WS_WMP_SendFCSubscribe(PILI_RTMP *r, PILI_AVal *subscribepath, RTMPError *error) {
    PILI_RTMPPacket packet;
    char pbuf[512], *pend = pbuf + sizeof(pbuf);
    char *enc;
    packet.m_nChannel = 0x03; /* control channel (invoke) */
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet.m_packetType = 0x14; /* INVOKE */
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;
    
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "FCSubscribe: %s", subscribepath->av_val);
    enc = packet.m_body;
    enc = PILI_AMF_EncodeString(enc, pend, &av_FCSubscribe);
    enc = PILI_AMF_EncodeNumber(enc, pend, ++r->m_numInvokes);
    *enc++ = PILI_AMF_NULL;
    enc = PILI_AMF_EncodeString(enc, pend, subscribepath);
    
    if (!enc)
        return FALSE;
    
    packet.m_nBodySize = enc - packet.m_body;
    
    return WS_WMP_RTMP_SendPacket(r, &packet, TRUE, error);
}




static const PILI_AVal av_NetStream_Failed = AVC("NetStream.Failed");
static const PILI_AVal av_NetStream_Play_Failed = AVC("NetStream.Play.Failed");
static const PILI_AVal av_NetStream_Play_StreamNotFound =
AVC("NetStream.Play.StreamNotFound");
static const PILI_AVal av_NetConnection_Connect_InvalidApp =
AVC("NetConnection.Connect.InvalidApp");
static const PILI_AVal av_NetStream_Play_Start = AVC("NetStream.Play.Start");
static const PILI_AVal av_NetStream_Play_Complete = AVC("NetStream.Play.Complete");
static const PILI_AVal av_NetStream_Play_Stop = AVC("NetStream.Play.Stop");
static const PILI_AVal av_NetStream_Seek_Notify = AVC("NetStream.Seek.Notify");
static const PILI_AVal av_NetStream_Pause_Notify = AVC("NetStream.Pause.Notify");
static const PILI_AVal av_NetStream_Play_UnpublishNotify =
AVC("NetStream.Play.UnpublishNotify");
static const PILI_AVal av_NetStream_Publish_Start = AVC("NetStream.Publish.Start");



 int WS_WMP_HandleInvoke(PILI_RTMP *r, const char *body, unsigned int nBodySize) {

     PILI_AMFObject obj;
     PILI_AVal method;
     
     int txn;
     int ret = 0, nRes;
     if (body[0] != 0x02) /* make sure it is a string method name we start with */
     {
         PILI_RTMP_Log(PILI_RTMP_LOGWARNING, "%s, Sanity failed. no string method in invoke packet",
                       __FUNCTION__);
         return 0;
     }
     
     nRes = PILI_AMF_Decode(&obj, body, nBodySize, FALSE);
     if (nRes < 0) {
         PILI_RTMP_Log(PILI_RTMP_LOGERROR, "%s, error decoding invoke packet", __FUNCTION__);
         return 0;
     }
     
     PILI_AMF_Dump(&obj);
     PILI_AMFProp_GetString(PILI_AMF_GetProp(&obj, NULL, 0), &method);
     txn = (int)PILI_AMFProp_GetNumber(PILI_AMF_GetProp(&obj, NULL, 1));
     PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, server invoking <%s>", __FUNCTION__, method.av_val);
     
     RTMPError error = {0};
     
     if (AVMATCH(&method, &av__result)) {
         PILI_AVal methodInvoked = {0};
         int i;
         
         for (i = 0; i < r->m_numCalls; i++) {
             if (r->m_methodCalls[i].num == txn) {
                 methodInvoked = r->m_methodCalls[i].name;
                 WS_WMP_AV_erase(r->m_methodCalls, &r->m_numCalls, i, FALSE);
                 break;
             }
         }
         if (!methodInvoked.av_val) {
             PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, received result id %d without matching request",
                           __FUNCTION__, txn);
             goto leave;
         }
         
         PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, received result for method call <%s>", __FUNCTION__,
                       methodInvoked.av_val);
         
         if (AVMATCH(&methodInvoked, &av_connect)) {
             if (r->Link.token.av_len) {
                 PILI_AMFObjectProperty p;
                 if (PILI_RTMP_FindFirstMatchingProperty(&obj, &av_secureToken, &p)) {
                     WS_WMP_PILI_DecodeTEA(&r->Link.token, &p.p_vu.p_aval);
                     //SendSecureTokenResponse(r, &p.p_vu.p_aval, &error);
                 }
             }
             if (r->Link.protocol & RTMP_FEATURE_WRITE) {
                 WS_WMP_SendReleaseStream(r, &error);
                 WS_WMP_SendFCPublish(r, &error);
             } else {
                // PILI_RTMP_SendServerBW(r, &error);
                 //PILI_RTMP_SendCtrl(r, 3, 0, 300, &error);
             }
             WS_WMP_RTMP_SendCreateStream(r, &error);
             
             if (!(r->Link.protocol & RTMP_FEATURE_WRITE)) {
                 /* Send the FCSubscribe if live stream or if subscribepath is set */
                 if (r->Link.subscribepath.av_len)
                     WS_WMP_SendFCSubscribe(r, &r->Link.subscribepath, &error);
                 else if (r->Link.lFlags & RTMP_LF_LIVE)
                     WS_WMP_SendFCSubscribe(r, &r->Link.playpath, &error);
             }
         } else if (AVMATCH(&methodInvoked, &av_createStream)) {
             r->m_stream_id = (int)PILI_AMFProp_GetNumber(PILI_AMF_GetProp(&obj, NULL, 3));
             
             if (r->Link.protocol & RTMP_FEATURE_WRITE) {
                 WS_WMP_SendPublish(r, &error);
             } else {
                 //if (r->Link.lFlags & RTMP_LF_PLST)
                     //PILI_SendPlaylist(r, &error);
                 //PILI_SendPlay(r, &error);
                 //PILI_RTMP_SendCtrl(r, 3, r->m_stream_id, r->m_nBufferMS, &error);
             }
         }
         free(methodInvoked.av_val);
     } else if (AVMATCH(&method, &av_onBWDone)) {
         if (!r->m_nBWCheckCounter)
             WS_WMP_SendCheckBW(r, &error);
     }else if (AVMATCH(&method, &av_ping)) {
         WS_WMP_SendPong(r, txn, &error);
     } else if (AVMATCH(&method, &av__onbwcheck)) {
         //PILI_SendCheckBWResult(r, txn, &error);
     } else if (AVMATCH(&method, &av__onbwdone)) {
         int i;
         for (i = 0; i < r->m_numCalls; i++)
             if (AVMATCH(&r->m_methodCalls[i].name, &av__checkbw)) {
                 WS_WMP_AV_erase(r->m_methodCalls, &r->m_numCalls, i, TRUE);
                 break;
             }
     }  else if (AVMATCH(&method, &av_onStatus)) {
         PILI_AMFObject obj2;
         PILI_AVal code, level;
         PILI_AMFProp_GetObject(PILI_AMF_GetProp(&obj, NULL, 3), &obj2);
         PILI_AMFProp_GetString(PILI_AMF_GetProp(&obj2, &av_code, -1), &code);
         PILI_AMFProp_GetString(PILI_AMF_GetProp(&obj2, &av_level, -1), &level);
         
         PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, onStatus: %s", __FUNCTION__, code.av_val);
         if (AVMATCH(&code, &av_NetStream_Failed) || AVMATCH(&code, &av_NetStream_Play_Failed) || AVMATCH(&code, &av_NetStream_Play_StreamNotFound) || AVMATCH(&code, &av_NetConnection_Connect_InvalidApp)) {
             r->m_stream_id = -1;
             
             int err_code;
             char msg[100];
             memset(msg, 0, 100);
             
             if (AVMATCH(&code, &av_NetStream_Failed)) {
                 err_code = PILI_RTMPErrorNetStreamFailed;
                 strcpy(msg, "NetStream failed.");
             } else if (AVMATCH(&code, &av_NetStream_Play_Failed)) {
                 err_code = PILI_RTMPErrorNetStreamPlayFailed;
                 strcpy(msg, "NetStream play failed.");
             } else if (AVMATCH(&code, &av_NetStream_Play_StreamNotFound)) {
                 err_code = PILI_RTMPErrorNetStreamPlayStreamNotFound;
                 strcpy(msg, "NetStream play stream not found.");
             } else if (AVMATCH(&code, &av_NetConnection_Connect_InvalidApp)) {
                 err_code = PILI_RTMPErrorNetConnectionConnectInvalidApp;
                 strcpy(msg, "NetConnection connect invalip app.");
             } else {
                 err_code = PILI_RTMPErrorUnknow;
                 strcpy(msg, "Unknow error.");
             }
             
             PILI_RTMPError_Alloc(&error, strlen(msg));
             error.code = err_code;
             strcpy(error.message, msg);
             
             WS_WMP_close(r);
             
             PILI_RTMPError_Free(&error);
             
             PILI_RTMP_Log(PILI_RTMP_LOGERROR, "Closing connection: %s", code.av_val);
         }
         
         
         else if (AVMATCH(&code, &av_NetStream_Publish_Start)) {
             int i;
             r->m_bPlaying = TRUE;
             for (i = 0; i < r->m_numCalls; i++) {
                 if (AVMATCH(&r->m_methodCalls[i].name, &av_publish)) {
                     WS_WMP_AV_erase(r->m_methodCalls, &r->m_numCalls, i, TRUE);
                     break;
                 }
             }
         }
         
         
         else if (AVMATCH(&code, &av_NetStream_Seek_Notify)) {
             r->m_read.flags &= ~RTMP_READ_SEEKING;
         }
         
         else if (AVMATCH(&code, &av_NetStream_Pause_Notify)) {
             if (r->m_pausing == 1 || r->m_pausing == 2) {
                 //PILI_RTMP_SendPause(r, FALSE, r->m_pauseStamp, &error);
                 r->m_pausing = 3;
             }
         }
     } else {
     }
 leave:
     PILI_AMF_Reset(&obj);
    return ret;
}
int WS_WMP_RTMP_ClientPacket(PILI_RTMP *r, PILI_RTMPPacket *packet) {
    int bHasMediaPacket = 0;
    switch (packet->m_packetType) {
        case 0x01:
            /* chunk size */
            WS_WMP_HandleChangeChunkSize(r, packet);
            break;
            
        case 0x03:
            /* bytes read report */
            PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, received: bytes read report", __FUNCTION__);
            break;
            
        case 0x04:
            /* ctrl */
            //PILI_HandleCtrl(r, packet);
            break;
            
        case 0x05:
            /* server bw */
            WS_WMP_HandleServerBW(r, packet);
            break;
            
        case 0x06:
            /* client bw */
            WS_WMP_HandleClientBW(r, packet);
            break;
            
            
        case 0x0F: /* flex stream send */
            PILI_RTMP_Log(PILI_RTMP_LOGDEBUG,
                          "%s, flex stream send, size %lu bytes, not supported, ignoring",
                          __FUNCTION__, packet->m_nBodySize);
            break;
            
        case 0x10: /* flex shared object */
            PILI_RTMP_Log(PILI_RTMP_LOGDEBUG,
                          "%s, flex shared object, size %lu bytes, not supported, ignoring",
                          __FUNCTION__, packet->m_nBodySize);
            break;
            
        case 0x11: /* flex message */
        {
            PILI_RTMP_Log(PILI_RTMP_LOGDEBUG,
                          "%s, flex message, size %lu bytes, not fully supported",
                          __FUNCTION__, packet->m_nBodySize);
            
            if (WS_WMP_HandleInvoke(r, packet->m_body + 1, packet->m_nBodySize - 1) == 1)
                bHasMediaPacket = 2;
            break;
        }
       
            
        case 0x13:
            PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, shared object, not supported, ignoring",
                          __FUNCTION__);
            break;
            
        case 0x14:
            /* invoke */
            PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, received: invoke %lu bytes", __FUNCTION__,
                          packet->m_nBodySize);
            /*PILI_RTMP_LogHex(packet.m_body, packet.m_nBodySize); */
            
            if (WS_WMP_HandleInvoke(r, packet->m_body, packet->m_nBodySize) == 1)
                bHasMediaPacket = 2;
            break;
        default:
            PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, unknown packet type received: 0x%02x", __FUNCTION__,
                          packet->m_packetType);
#ifdef _DEBUG
            PILI_RTMP_LogHex(PILI_RTMP_LOGDEBUG, packet->m_body, packet->m_nBodySize);
#endif
    }
    if (bHasMediaPacket) {
        r->m_bPlaying = 1;
    }
    return bHasMediaPacket;
}

 int WS_WMP_RTMP_SendPacket(PILI_RTMP *r, PILI_RTMPPacket *packet, int queue, RTMPError *error) {
    const PILI_RTMPPacket *prevPacket = r->m_vecChannelsOut[packet->m_nChannel];
    uint32_t last = 0;
    int nSize;
    int hSize, cSize;
    char *header, *hptr, *hend, hbuf[RTMP_MAX_HEADER_SIZE], c;
    uint32_t t;
    char *buffer, *tbuf = NULL, *toff = NULL;
    int nChunkSize;
    int tlen;
    
    if (prevPacket && packet->m_headerType != RTMP_PACKET_SIZE_LARGE) {
        /* compress a bit by using the prev packet's attributes */
        if (prevPacket->m_nBodySize == packet->m_nBodySize && prevPacket->m_packetType == packet->m_packetType && packet->m_headerType == RTMP_PACKET_SIZE_MEDIUM)
            packet->m_headerType = RTMP_PACKET_SIZE_SMALL;
        
        if (prevPacket->m_nTimeStamp == packet->m_nTimeStamp && packet->m_headerType == RTMP_PACKET_SIZE_SMALL)
            packet->m_headerType = RTMP_PACKET_SIZE_MINIMUM;
        last = prevPacket->m_nTimeStamp;
    }
    
    if (packet->m_headerType > 3) /* sanity */
    {
        if (error) {
            char *msg = "Sanity failed.";
            PILI_RTMPError_Alloc(error, strlen(msg));
            error->code = PILI_RTMPErrorSanityFailed;
            strcpy(error->message, msg);
        }
        
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "sanity failed!! trying to send header of type: 0x%02x.",
                      (unsigned char)packet->m_headerType);
        
        return FALSE;
    }
    
    nSize = packetSize[packet->m_headerType];
    hSize = nSize;
    cSize = 0;
    t = packet->m_nTimeStamp - last;
    
    if (packet->m_body) {
        header = packet->m_body - nSize;
        hend = packet->m_body;
    } else {
        header = hbuf + 6;
        hend = hbuf + sizeof(hbuf);
    }
    
    if (packet->m_nChannel > 319)
        cSize = 2;
    else if (packet->m_nChannel > 63)
        cSize = 1;
    if (cSize) {
        header -= cSize;
        hSize += cSize;
    }
    
    if (nSize > 1 && t >= 0xffffff) {
        header -= 4;
        hSize += 4;
    }
    
    hptr = header;
    c = packet->m_headerType << 6;
    switch (cSize) {
        case 0:
            c |= packet->m_nChannel;
            break;
        case 1:
            break;
        case 2:
            c |= 1;
            break;
    }
    *hptr++ = c;
    if (cSize) {
        int tmp = packet->m_nChannel - 64;
        *hptr++ = tmp & 0xff;
        if (cSize == 2)
            *hptr++ = tmp >> 8;
    }
    
    if (nSize > 1) {
        hptr = PILI_AMF_EncodeInt24(hptr, hend, t > 0xffffff ? 0xffffff : t);
    }
    
    if (nSize > 4) {
        hptr = PILI_AMF_EncodeInt24(hptr, hend, packet->m_nBodySize);
        *hptr++ = packet->m_packetType;
    }
    
    if (nSize > 8)
        hptr += WS_WMP_EncodeInt32LE(hptr, packet->m_nInfoField2);
    
    if (nSize > 1 && t >= 0xffffff)
        hptr = PILI_AMF_EncodeInt32(hptr, hend, t);
    
    nSize = packet->m_nBodySize;
    buffer = packet->m_body;
    nChunkSize = r->m_outChunkSize;
    
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG2, "%s: fd=%d, size=%d", __FUNCTION__, r->m_sb.sb_socket,
                  nSize);
    /* send all chunks in one HTTP request */
    if (r->Link.protocol & RTMP_FEATURE_HTTP) {
        int chunks = (nSize + nChunkSize - 1) / nChunkSize;
        if (chunks > 1) {
            tlen = chunks * (cSize + 1) + nSize + hSize;
            tbuf = malloc(tlen);
            if (!tbuf)
                return FALSE;
            toff = tbuf;
        }
    }
    while (nSize + hSize) {
        int wrote;
        
        if (nSize < nChunkSize)
            nChunkSize = nSize;
        
        //PILI_RTMP_LogHexString(PILI_RTMP_LOGDEBUG2, (uint8_t *)header, hSize);
        //PILI_RTMP_LogHexString(PILI_RTMP_LOGDEBUG2, (uint8_t *)buffer, nChunkSize);
        if (tbuf) {
            memcpy(toff, header, nChunkSize + hSize);
            toff += nChunkSize + hSize;
        } else {
            wrote = WS_WMP_WriteN(r, header, nChunkSize + hSize, error);
            if (!wrote)
                return FALSE;
        }
        nSize -= nChunkSize;
        buffer += nChunkSize;
        hSize = 0;
        
        if (nSize > 0) {
            header = buffer - 1;
            hSize = 1;
            if (cSize) {
                header -= cSize;
                hSize += cSize;
            }
            *header = (0xc0 | c);
            if (cSize) {
                int tmp = packet->m_nChannel - 64;
                header[1] = tmp & 0xff;
                if (cSize == 2)
                    header[2] = tmp >> 8;
            }
        }
    }
    if (tbuf) {
        int wrote = WS_WMP_WriteN(r, tbuf, toff - tbuf, error);
        free(tbuf);
        tbuf = NULL;
        if (!wrote)
            return FALSE;
    }
    
    /* we invoked a remote method */
    if (packet->m_packetType == 0x14) {
        PILI_AVal method;
        char *ptr;
        ptr = packet->m_body + 1;
        PILI_AMF_DecodeString(ptr, &method);
        PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "Invoking %s", method.av_val);
        /* keep it in call queue till result arrives */
        if (queue) {
            int txn;
            ptr += 3 + method.av_len;
            txn = (int)PILI_AMF_DecodeNumber(ptr);
            WS_WMP_AV_queue(&r->m_methodCalls, &r->m_numCalls, &method, txn);
        }
    }
    
    if (!r->m_vecChannelsOut[packet->m_nChannel])
        r->m_vecChannelsOut[packet->m_nChannel] = malloc(sizeof(PILI_RTMPPacket));
    memcpy(r->m_vecChannelsOut[packet->m_nChannel], packet, sizeof(PILI_RTMPPacket));
    return TRUE;
}


int ws_wmp_module_init(void *arg, void *err)
{
    PILI_RTMP *parent_rtmp = (PILI_RTMP *)arg;
    RTMPError *error = (RTMPError*)err;
    parent_rtmp->m_userData = (void*)PILI_RTMP_Alloc();
    PILI_RTMP *r = (PILI_RTMP*)parent_rtmp->m_userData;
    PILI_RTMP_Init(r);
    r->m_userData = (void*)malloc(sizeof(int));
    if(!PILI_RTMP_SetupURL(r,parent_rtmp->Link.tcUrl.av_val, err))
    {
        PILI_RTMP_Log(PILI_RTMP_LOGERROR,"SetupURL Err\n");
        PILI_RTMP_Free(r);
        return -1;
    }
    r->Link.protocol |= RTMP_FEATURE_WRITE;

    pt_module_init();
    if(WS_WMP_RTMP_Connect(r, NULL, err) < 0 )
    {
        WS_WMP_close(r);
        return -1;
    }
    if (!WS_WMP_ConnectStream(r,0,err)){
        PILI_RTMP_Log(PILI_RTMP_LOGERROR,"ConnectStream Err\n");
        WS_WMP_close(r);
        return -1;
    }
    return 1;
    
    
    
}
int ws_wmp_module_release(void *arg)
{
    PILI_RTMP* rtmp_parent = (PILI_RTMP *)arg;
    PILI_RTMP *r = (PILI_RTMP *)rtmp_parent->m_userData;
    if (r->m_userData) {
        WS_WMP_close(r);
        free(r->m_userData);
        r->m_userData = NULL;
        PILI_RTMP_Free(r);
    }
    return TRUE;

}

int ws_wmp_module_push(void* rtmp, void* ptr, uint32_t size, void* err)
{
    PILI_RTMP *parent_rtmp = (PILI_RTMP *)rtmp;
    static const PILI_AVal av_setDataFrame = AVC("@setDataFrame");
    RTMPError *error = (RTMPError *)err;
    const char *buf = ptr;
    PILI_RTMP *r = (PILI_RTMP *)parent_rtmp->m_userData;
    PILI_RTMPPacket *pkt = &r->m_write;
    char *pend, *enc;
    int s2 = size, ret, num;
    
    pkt->m_nChannel = 0x04; /* source channel */
    pkt->m_nInfoField2 = r->m_stream_id;
    
    if (s2) {
        if (!pkt->m_nBytesRead) {
            if (size < 11) {
                /* FLV pkt too small */
                return 0;
            }
            
            if (buf[0] == 'F' && buf[1] == 'L' && buf[2] == 'V') {
                buf += 13;
                s2 -= 13;
            }
            
            pkt->m_packetType = *buf++;
            pkt->m_nBodySize = PILI_AMF_DecodeInt24(buf);
            buf += 3;
            pkt->m_nTimeStamp = PILI_AMF_DecodeInt24(buf);
            buf += 3;
            pkt->m_nTimeStamp |= *buf++ << 24;
            buf += 3;
            s2 -= 11;
            
            if (((pkt->m_packetType == 0x08 || pkt->m_packetType == 0x09) &&
                 !pkt->m_nTimeStamp) ||
                pkt->m_packetType == 0x12) {
                pkt->m_headerType = RTMP_PACKET_SIZE_LARGE;
                if (pkt->m_packetType == 0x12)
                    pkt->m_nBodySize += 16;
            } else {
                pkt->m_headerType = RTMP_PACKET_SIZE_MEDIUM;
            }
            
            if (!PILI_RTMPPacket_Alloc(pkt, pkt->m_nBodySize)) {
                PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "%s, failed to allocate packet", __FUNCTION__);
                return FALSE;
            }
            enc = pkt->m_body;
            pend = enc + pkt->m_nBodySize;
            if (pkt->m_packetType == 0x12) {
                enc = PILI_AMF_EncodeString(enc, pend, &av_setDataFrame);
                pkt->m_nBytesRead = enc - pkt->m_body;
            }
        } else {
            enc = pkt->m_body + pkt->m_nBytesRead;
        }
        num = pkt->m_nBodySize - pkt->m_nBytesRead;
        if (num > s2)
            num = s2;
        memcpy(enc, buf, num);
        pkt->m_nBytesRead += num;
        s2 -= num;
        buf += num;
        if (pkt->m_nBytesRead == pkt->m_nBodySize) {
            ret = WS_WMP_RTMP_SendPacket(r, pkt, FALSE, error);
            PILI_RTMPPacket_Free(pkt);
            pkt->m_nBytesRead = 0;
            if (!ret)
                return -1;
            buf += 4;
            s2 -= 4;
            if (s2 < 0)
                return TRUE;
        }
    }
    return size + s2;

}
