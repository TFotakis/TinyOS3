#include <zconf.h>
#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_streams.h"
typedef struct socket_control_block SCB;
typedef struct listener_requests {
    Fid_t fid;
    FCB *fcb;
    SCB *scb;
    CondVar cv;
    int isServed;
} Request;
typedef struct listener_extra_properties {
    rlnode requests;
    CondVar cv;
} ListenerProps;
typedef struct peer_extra_properties {
    PipeCB *receiver, *transmitter;
    SCB *otherPeer;
} PeerProps;
typedef struct socket_control_block {
    Fid_t fid;
    FCB *fcb;
    SocketType socketType;
    port_t boundPort;
    int refcount;
    union {
        ListenerProps *listenerProps;
        PeerProps *peerProps;
    } extraProps;
} SCB;
SCB *get_scb(Fid_t sock) {
    if (sock < 0 || sock > MAX_FILEID)return NULL;
    FCB *fcb = get_fcb(sock);
    if (fcb == NULL)return NULL;
    return (SCB *) fcb->streamobj;
}
SCB *Portmap[MAX_PORT + 1] = {NULL};
int socket_close(void *tmpSCB) {
    SCB *scb = (SCB *) tmpSCB;
    if (scb == NULL) return -1;
    assert(scb != NULL);
    switch (scb->socketType) {
        case UNBOUND:
            break;
        case LISTENER:
            Portmap[scb->boundPort] = NULL;
            while (!is_rlist_empty(&scb->extraProps.listenerProps->requests)) {
                rlnode *reqNode = rlist_pop_front(&scb->extraProps.listenerProps->requests);
                Cond_Signal(&reqNode->request->cv);
            }
            free(scb->extraProps.listenerProps);
            break;
        case PEER:
            if (scb->extraProps.peerProps->otherPeer) {
                pipe_closeReader(scb->extraProps.peerProps->receiver);
                pipe_closeWriter(scb->extraProps.peerProps->transmitter);
                scb->extraProps.peerProps->otherPeer->extraProps.peerProps->otherPeer = NULL;
            }
            free(scb->extraProps.peerProps);
            break;
    }
    free(NULL);
    free(scb);
    return 0;
}
int socket_read(void *tmpScb, char *buf, unsigned int size) {
    Mutex_Lock(&kernel_mutex);
    SCB *scb = (SCB *) tmpScb;
    int isPeer = scb->socketType == PEER;
    PipeCB *pipeCB = scb->extraProps.peerProps->receiver;
    Mutex_Unlock(&kernel_mutex);
    if (isPeer)return pipe_read(pipeCB, buf, size);
    return -1;
}
int socket_write(void *tmpScb, const char *buf, unsigned int size) {
    Mutex_Lock(&kernel_mutex);
    SCB *scb = (SCB *) tmpScb;
    int isPeer = scb->socketType == PEER;
    PipeCB *pipeCB = scb->extraProps.peerProps->transmitter;
    Mutex_Unlock(&kernel_mutex);
    if (isPeer)return pipe_write(pipeCB, buf, size);
    return -1;
}
file_ops socketFuncs = {
        .Open = NULL,
        .Read = socket_read,
        .Write = socket_write,
        .Close = socket_close
};
Fid_t Socket(port_t port) {
    if (port < 0 || port > MAX_PORT)return NOFILE;
    Fid_t fid;
    FCB *fcb;
    Mutex_Lock(&kernel_mutex);
    if (!FCB_reserve(1, &fid, &fcb)) {
        Mutex_Unlock(&kernel_mutex);
        return NOFILE;
    }
    SCB *scb = (SCB *) xmalloc(sizeof(SCB));
    scb->fid = fid;
    scb->fcb = fcb;
    scb->socketType = UNBOUND;
    scb->boundPort = port;
    scb->refcount = 0;
    fcb->streamobj = scb;
    fcb->streamfunc = &socketFuncs;
    Mutex_Unlock(&kernel_mutex);
    return fid;
}
int Listen(Fid_t sock) {
    Mutex_Lock(&kernel_mutex);
    SCB *scb = get_scb(sock);
    if (scb == NULL || scb->socketType != UNBOUND || scb->boundPort <= 0 || Portmap[scb->boundPort] != NULL) {
        Mutex_Unlock(&kernel_mutex);
        return -1;
    }
    scb->socketType = LISTENER;
    scb->extraProps.listenerProps = (ListenerProps *) xmalloc(sizeof(ListenerProps));
    scb->extraProps.listenerProps->cv = COND_INIT;
    rlnode_new(&scb->extraProps.listenerProps->requests);
    Portmap[scb->boundPort] = scb;
    Mutex_Unlock(&kernel_mutex);
    return 0;
}
Fid_t Accept(Fid_t lsock) {
    Mutex_Lock(&kernel_mutex);
    SCB *listenerSCB = get_scb(lsock);
    if (listenerSCB == NULL || listenerSCB->socketType != LISTENER) {
        Mutex_Unlock(&kernel_mutex);
        return NOFILE;
    }
    while (is_rlist_empty(&listenerSCB->extraProps.listenerProps->requests) && get_scb(lsock)) {
        Cond_Wait(&kernel_mutex, &listenerSCB->extraProps.listenerProps->cv);
    }
    rlnode *requestNode = rlist_pop_front(&listenerSCB->extraProps.listenerProps->requests);
    Request *request = requestNode->request;
    if (!get_scb(lsock)) {
        Mutex_Unlock(&kernel_mutex);
        Cond_Signal(&request->cv);
        return NOFILE;
    }
    Mutex_Unlock(&kernel_mutex);
    Fid_t peer1fid = Socket(NOPORT);
    if (peer1fid == NOFILE) {
        Cond_Signal(&request->cv);
        return NOFILE;
    }
    Mutex_Lock(&kernel_mutex);
    SCB *peer1 = get_scb(peer1fid);
    SCB *peer2 = request->scb;
    peer1->extraProps.peerProps = (PeerProps *) xmalloc(sizeof(PeerProps));
    peer2->extraProps.peerProps = (PeerProps *) xmalloc(sizeof(PeerProps));
    pipe_t pipe1, pipe2;
    Fid_t fid[2];
    FCB *fcb[2];
    fid[0] = request->fid;
    fid[1] = peer1fid;
    fcb[0] = request->fcb;
    fcb[1] = get_fcb(peer1fid);
    PipeCB *pipeCB1 = PipeNoReserving(&pipe1, fid, fcb);
    fid[0] = peer1fid;
    fid[1] = request->fid;
    fcb[0] = get_fcb(peer1fid);
    fcb[1] = request->fcb;
    PipeCB *pipeCB2 = PipeNoReserving(&pipe2, fid, fcb);
    peer1->extraProps.peerProps->transmitter = pipeCB1;
    peer1->extraProps.peerProps->receiver = pipeCB2;
    peer1->extraProps.peerProps->otherPeer = peer2;
    peer1->socketType = PEER;
    peer2->extraProps.peerProps->transmitter = pipeCB2;
    peer2->extraProps.peerProps->receiver = pipeCB1;
    peer2->extraProps.peerProps->otherPeer = peer1;
    peer2->socketType = PEER;
    request->isServed = 1;
    Mutex_Unlock(&kernel_mutex);
    Cond_Signal(&request->cv);
    return peer1fid;
}
int Connect(Fid_t sock, port_t port, timeout_t timeout) {
    Mutex_Lock(&kernel_mutex);
    SCB *scb = get_scb(sock);
    if (port < 0 || port >= MAX_PORT || Portmap[port] == NULL || Portmap[port]->socketType != LISTENER ||
            scb->socketType != UNBOUND) {
        Mutex_Unlock(&kernel_mutex);
        return -1;
    }
    Request *request = (Request *) xmalloc(sizeof(Request));
    request->cv = COND_INIT;
    request->isServed = 0;
    request->fid = sock;
    request->fcb = get_fcb(sock);
    request->scb = get_scb(sock);
    rlnode node;
    rlnode_init(&node, request);
    rlist_push_back(&Portmap[port]->extraProps.listenerProps->requests, &node);
    Cond_Signal(&Portmap[port]->extraProps.listenerProps->cv);
    Cond_Wait_with_timeout(&kernel_mutex, &request->cv, timeout);
    rlist_remove(&node);
    Mutex_Unlock(&kernel_mutex);
    return request->isServed - 1;
}
int ShutDown(Fid_t sock, shutdown_mode how) {
    Mutex_Lock(&kernel_mutex);
    SCB *scb = get_scb(sock);
    int retVal = -1;
    switch (how) {
        case SHUTDOWN_READ:
            retVal = pipe_closeReader(scb->extraProps.peerProps->receiver);
            break;
        case SHUTDOWN_WRITE:
            retVal = pipe_closeWriter(scb->extraProps.peerProps->transmitter);
            break;
        case SHUTDOWN_BOTH:
            retVal = socket_close(scb);
            break;
    }
    Mutex_Unlock(&kernel_mutex);
    return retVal;
}