/*****************************************************************************
 * $Source$
 * $Author$
 * $Date$
 * $Revision$
 *****************************************************************************/

#ifndef _CHARMPP_H_
#define _CHARMPP_H_

#include <stdlib.h>
#include <memory.h>
#include "charm.h"

class CMessage_CkArgMsg {
public: static int __idx;
};
#define CK_ALIGN(val,to) (((val)+(to)-1)&~((to)-1))

#include "cklists.h"
#include "init.h"
#include "pup.h"
#include "debug-charm.h"

void CkPupMessage(PUP::er &p,void **atMsg,int fast_and_dirty=1);

class Message { //Superclass of all Charm++ messages
public:
	void operator delete(void *ptr) { CkFreeMsg(ptr); }
	/* This pup routine only packs the message itself, *not* the
	message header.  Use CkPupMessage instead of calling this directly. */
	void pup(PUP::er &p);
};

#include "CkMarshall.decl.h"

class CkMarshallMsg : public CMessage_CkMarshallMsg {
public: 
	char *msgBuf;
};

/********************* Superclass of all Chares ******************/
class Chare {
  protected:
    CkChareID thishandle;
  public:
    Chare(CkMigrateMessage *m) {}
    void *operator new(size_t, void *ptr) { return ptr; };
#if CMK_COMPILEMODE_ANSI
    void operator delete(void*, void*) {};
#endif
    void *operator new(size_t s) { return malloc(s); }
    void operator delete(void *ptr) { free(ptr); }
    Chare() { CkGetChareID(&thishandle); }
    virtual ~Chare(); //<- needed for *any* child to have a virtual destructor
    virtual void pup(PUP::er &p);//<- pack/unpack routine
};

class Group : public Chare { //Superclass of all Groups
  protected:
    CkGroupID thisgroup;
  public:
    Group(CkMigrateMessage *m) {}
    Group() { thisgroup = CkGetGroupID(); }
    virtual ~Group(); //<- needed for *any* child to have a virtual destructor

    virtual void pup(PUP::er &p);//<- pack/unpack routine
};

class NodeGroup : public Chare { //Superclass of all NodeGroups
  protected:
    CkGroupID thisgroup;
  public:
    CmiNodeLock __nodelock;
    NodeGroup() { thisgroup=CkGetNodeGroupID(); __nodelock=CmiCreateLock();}
    ~NodeGroup() { CmiDestroyLock(__nodelock); }
};

/*Message delegation support, where you send a message via
a proxy normally, but the message ends up routed via a 
special delegateMgr group.
*/
class CkDelegateMgr;

class CProxyBase_Delegatable {
  private:
    CkGroupID delegatedTo;
  protected: //Never allocate CProxyBase_Delegatable's-- only subclass them.
    CProxyBase_Delegatable() { delegatedTo.pe = -1; }
    CProxyBase_Delegatable(CkGroupID dTo) { delegatedTo = dTo; }
  public:
    void ckDelegate(CkGroupID to) {delegatedTo=to;}
    void ckUndelegate(void) {delegatedTo.pe =-1;}
    int ckIsDelegated(void) const {return (delegatedTo.pe != -1);}
    CkGroupID ckDelegatedIdx(void) const {return delegatedTo;}
    CkDelegateMgr *ckDelegatedTo(void) const {
    	return (CkDelegateMgr *)CkLocalBranch(delegatedTo);
    }
    void pup(PUP::er &p) {
      p|delegatedTo;
    }
};
PUPmarshall(CProxyBase_Delegatable)

/*These disambiguation macros are needed to support
  multiple inheritance in Chares (Groups, Arrays).
  They resolve ambiguous accessor calls to the parent "super".
  Because mutator routines need to change *all* the base
  classes, mutators are generated in xi-symbol.C.
*/
#define CK_DISAMBIG_DELEGATABLE(super) \
    int ckIsDelegated(void) const {return super::ckIsDelegated();}\
    CkGroupID ckDelegatedIdx(void) const {return super::ckDelegatedIdx();}\
    CkDelegateMgr *ckDelegatedTo(void) const {\
    	return super::ckDelegatedTo();\
    }\



/*The base classes of each proxy type
*/
class CProxy_Chare : public CProxyBase_Delegatable {
  private:
    CkChareID _ck_cid;
  public:
    CProxy_Chare() { 
#ifndef CMK_OPTIMIZE
	_ck_cid.onPE=0; _ck_cid.objPtr=0;
#endif
    }
    CProxy_Chare(const CkChareID &c) : _ck_cid(c) {}
    const CkChareID &ckGetChareID(void) const {return _ck_cid;}
    void ckSetChareID(const CkChareID &c) {_ck_cid=c;}
    void pup(PUP::er &p) {
    	CProxyBase_Delegatable::pup(p);
    	p(_ck_cid.onPE);
    	p(_ck_cid.magic);
    	//Copy the pointer as straight bytes
    	p((void *)&_ck_cid.objPtr,sizeof(_ck_cid.objPtr));
    }
};
PUPmarshall(CProxy_Chare)

#define CK_DISAMBIG_CHARE(super) \
	CK_DISAMBIG_DELEGATABLE(super) \
	const CkChareID &ckGetChareID(void) const\
    	   {return super::ckGetChareID();} \


class CProxy_Group : public CProxyBase_Delegatable {
  private:
    CkGroupID _ck_gid;
  public:
    CProxy_Group() { }
    CProxy_Group(CkGroupID g) 
    	:CProxyBase_Delegatable(),_ck_gid(g) {}
    CProxy_Group(CkGroupID g,CkGroupID dTo) 
    	:CProxyBase_Delegatable(dTo),_ck_gid(g) {}
    CkChareID ckGetChareID(void) const { 
    	CkChareID ret;
    	ret.onPE=CkMyPe();
    	ret.magic=0;//<- fake "chare type" value
    	ret.objPtr=CkLocalBranch(_ck_gid);
    	return ret; 
    }
    CkGroupID ckGetGroupID(void) const {return _ck_gid;}
    void ckSetGroupID(CkGroupID g) {_ck_gid=g;}
    void pup(PUP::er &p) {
    	CProxyBase_Delegatable::pup(p);
	p | _ck_gid;
    }
};
PUPmarshall(CProxy_Group)
#define CK_DISAMBIG_GROUP(super) \
	CK_DISAMBIG_DELEGATABLE(super) \
	CkChareID ckGetChareID(void) const\
    	   {return super::ckGetChareID();} \
	CkGroupID ckGetGroupID(void) const\
    	   {return super::ckGetGroupID();} \
	


class CProxyElement_Group : public CProxy_Group {
  private:
    int _onPE;    
  public:
    CProxyElement_Group() { }
    CProxyElement_Group(CkGroupID g,int onPE)
	: CProxy_Group(g),_onPE(onPE) {}
    CProxyElement_Group(CkGroupID g,int onPE,CkGroupID dTo)
	: CProxy_Group(g,dTo),_onPE(onPE) {}
    
    int ckGetGroupPe(void) const {return _onPE;}
    void pup(PUP::er &p) {
    	CProxy_Group::pup(p);
    	p(_onPE);
    }
};
PUPmarshall(CProxyElement_Group)
#define CK_DISAMBIG_GROUP_ELEMENT(super) \
	CK_DISAMBIG_GROUP(super) \
	int ckGetGroupPe(void) const\
    	   {return super::ckGetGroupPe();} \


/* These classes exist to provide chare indices for the basic
 chare types.*/
class CkIndex_Chare { public:
    static int __idx;//Fake chare index for registration
};
class CkIndex_ArrayBase { public:
    static int __idx;//Fake chare index for registration
};
class CkIndex_Group { public:
    static int __idx;//Fake chare index for registration
};

typedef CkIndex_Group CkIndex_NodeGroup;
typedef CProxy_Group CProxy_NodeGroup;
typedef CProxyElement_Group CProxyElement_NodeGroup;
typedef CkGroupID CkNodeGroupID;


class CkArray;
class CkArrayIndexMax;

class CkArrayID {
	CkGroupID _gid;
public:
	CkArrayID() : _gid() { }
	CkArrayID(CkGroupID g) :_gid(g) {}
	operator CkGroupID() const {return _gid;}
	CkArray *ckLocalBranch(void) const
		{ return (CkArray *)CkLocalBranch(_gid); }
	static CkArray *CkLocalBranch(CkArrayID id) 
		{ return (CkArray *)::CkLocalBranch(id); }
	void pup(PUP::er &p) {p | _gid; }
};
PUPmarshall(CkArrayID)

//(CProxy_ArrayBase is defined in ckarray.h)

//an "interface" class-- all delegated messages are routed via a DelegateMgr.  
// The default action is to deliver the message directly.
class CkDelegateMgr : public Group {
  public:
    virtual ~CkDelegateMgr(); //<- so children can have virtual destructor
    virtual void ChareSend(int ep,void *m,const CkChareID *c);
    virtual void GroupSend(int ep,void *m,int onPE,CkGroupID g);
    virtual void GroupBroadcast(int ep,void *m,CkGroupID g);
    virtual void NodeGroupSend(int ep,void *m,int onNode,CkNodeGroupID g);
    virtual void NodeGroupBroadcast(int ep,void *m,CkNodeGroupID g);
    virtual void ArrayCreate(int ep,void *m,const CkArrayIndexMax &idx,int onPE,CkArrayID a);
    virtual void ArraySend(int ep,void *m,const CkArrayIndexMax &idx,CkArrayID a);
    virtual void ArrayBroadcast(int ep,void *m,CkArrayID a);
};


class CkQdMsg {
  public:
    void *operator new(size_t s) { return CkAllocMsg(0,s,0); }
    void operator delete(void* ptr) { CkFreeMsg(ptr); }
    static void *alloc(int, size_t s, int*, int) {
      return CkAllocMsg(0,s,0);
    }
    static void *pack(CkQdMsg *m) { return (void*) m; }
    static CkQdMsg *unpack(void *buf) { return (CkQdMsg*) buf; }
};

class CkThrCallArg {
  public:
    void *msg;
    void *obj;
    CkThrCallArg(void *m, void *o) : msg(m), obj(o) {}
};

extern void _REGISTER_BASE(int didx, int bidx);
extern void _REGISTER_DONE(void);

static inline void _CHECK_CID(CkChareID, int){}

#include "readonly.h"
#include "ckarray.h"
#include "ckstream.h"
#include "CkFutures.decl.h"
#include "tempo.h"
#include "waitqd.h"
#include "sdag.h"

#endif
