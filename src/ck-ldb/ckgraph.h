/** \file ckgraph.h
 *
 *  Author: Abhinav S Bhatele
 *  Date Created: October 29th, 2010
 *  E-mail: bhatele@illinois.edu
 */

/**
 *  \addtogroup CkLdb
 */

/*@{*/

#ifndef _CKGRAPH_H_
#define _CKGRAPH_H_

#include "BaseLB.h"
#include <vector>

class ProcInfo
{
  friend class ProcArray;

public:
  ProcInfo() = default;
  ProcInfo(int i, double ov, double tl, double sp, bool avail)
      : id(i), _overhead(ov), _totalLoad(tl), _pe_speed(sp), available(avail)
  {
  }
  inline int getProcId() const { return id; }
  inline void setProcId(int _id) { id = _id; }
  inline double getTotalLoad() const { return _totalLoad; }
  inline double& overhead() { return _overhead; }
  inline double& totalLoad() { return _totalLoad; }
  inline double& pe_speed() { return _pe_speed; }
  inline bool isAvailable() const { return available; }

protected:
  int id;             // CkMyPe of the processor
  double _overhead;   // previously called background load (bg_walltime)
  double _totalLoad;  // includes object_load + overhead
  double _pe_speed;   // CPU speed
  bool available;     // if the processor is available
};

class ProcArray
{
public:
  ProcArray(BaseLB::LDStats* stats);
  inline double getAverageLoad() const { return avgLoad; }
  void resetTotalLoad();

  // vector containing the list of processors
  std::vector<ProcInfo> procs;

protected:
  double avgLoad;
};

class Edge
{
  friend class ObjGraph;

public:
  Edge(int _id, int _msgs, int _bytes) : id(_id), msgs(_msgs), bytes(_bytes) {}
  inline int getNeighborId() const { return id; }
  inline int getNumMsgs() const { return msgs; }
  inline int getNumBytes() const { return bytes; }
  inline void setNumBytes(int _bytes) { bytes = _bytes; }

private:
  int id;     // id of the neighbor = index of the neighbor vertex
              // in the vector 'vertices'
  int msgs;   // number of messages exchanged
  int bytes;  // total number of bytes exchanged
};

class McastSrc
{
  friend class ObjGraph;

public:
  McastSrc(int _numDest, int _msgs, int _bytes)
      : numDest(_numDest), msgs(_msgs), bytes(_bytes)
  {
  }

  inline int getNumMsgs() const { return msgs; }
  inline int getNumBytes() const { return bytes; }
  inline void setNumBytes(int _bytes) { bytes = _bytes; }

  std::vector<int> destList;

private:
  int numDest;  // number of destination for this multicast
  int msgs;     // number of messages exchanged
  int bytes;    // total number of bytes exchanged
};

class McastDest
{
  friend class ObjGraph;

public:
  McastDest(int _src, int _offset, int _msgs, int _bytes)
      : src(_src), offset(_offset), msgs(_msgs), bytes(_bytes)
  {
  }

  inline int getSrc() const { return src; }
  inline int getOffset() const { return offset; }
  inline int getNumMsgs() const { return msgs; }
  inline int getNumBytes() const { return bytes; }
  inline void setNumBytes(int _bytes) { bytes = _bytes; }

private:
  int src;     // src of multicast being received
  int offset;  // multicast list which this message belongs to
  int msgs;    // number of messages exchanged
  int bytes;   // total number of bytes exchanged
};

class Vertex
{
  friend class ObjGraph;

public:
  Vertex() = default;
  Vertex(int i, double cl, bool mig, int curpe, int newpe = -1, size_t pupsize = 0)
      : id(i),
        compLoad(cl),
        migratable(mig),
        currPe(curpe),
        newPe(newpe),
        pupSize(pupsize)
  {
  }

  inline int getVertexId() const { return id; }
  inline double getVertexLoad() const { return compLoad; }
  inline int getCurrentPe() const { return currPe; }
  inline int getNewPe() const { return newPe; }
  inline void setNewPe(int _newpe) { newPe = _newpe; }
  inline bool isMigratable() const { return migratable; }

  // list of vertices this vertex sends messages to and receives from
  std::vector<Edge> sendToList;
  std::vector<Edge> recvFromList;
  std::vector<McastSrc> mcastToList;
  std::vector<McastDest> mcastFromList;
  double getCompLoad() const { return compLoad; }
  void setCompLoad(double s) { compLoad = s; }
  int getCurrPe() const { return currPe; }
  void setCurrPe(int s) { currPe = s; }

private:
  int id;           // index in the LDStats array
  double compLoad;  // computational load (walltime in LDStats)
  bool migratable;  // migratable or non-migratable
  int currPe;       // current processor assignment
  int newPe;        // new processor assignment after load balancing
  size_t pupSize;
};

class ObjGraph
{
public:
  ObjGraph(BaseLB::LDStats* stats);
  void convertDecisions(BaseLB::LDStats* stats);

  // all vertices in the graph. Each vertex corresponds to a chare
  std::vector<Vertex> vertices;
};

#endif // _CKGRAPH_H_

/*@}*/
