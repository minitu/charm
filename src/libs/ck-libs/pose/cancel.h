/// List to store event cancellations
#ifndef CANCEL_H
#define CANCEL_H

/// A single event cancellation
class CancelNode { 
 public:
  /// Timestamp of event to be cancelled
  POSE_TimeType timestamp;   
  /// Event ID of event to be cancelled
  eventID evID;    
  /// Next cancellation in list
  CancelNode *next;
  /// Basic Constructor
  CancelNode() { }
  /// Initializing Constructor
  CancelNode(POSE_TimeType ts, eventID e) { timestamp = ts; evID = e; next = NULL; }
  /// Dump all datafields
  void dump() {
#if USE_LONG_TIMESTAMPS
    CkPrintf("[timestamp=%lld ", timestamp); evID.dump();  CkPrintf("] ");
#else
    CkPrintf("[timestamp=%d ", timestamp); evID.dump();  CkPrintf("] ");
#endif
  }
  /// Pack/unpack/sizing operator
  void pup(PUP::er &p) { p(timestamp); evID.pup(p); }
};

/// A list of event cancellations
class CancelList { 
  /// Number of cancellations in list
  int count;
  /// Timestamp of earliest cancellation in list
  /** Set to -1 if no cancellations present */
  POSE_TimeType earliest;        
  /// The list of cancellations
  CancelNode *cancellations;
  /// Pointer to a particular node in cancellations
  CancelNode *current; 
 public:
  /// Initializing Constructor
  CancelList() { count = 0; earliest = POSE_UnsetTS; cancellations = current = NULL; }
  /// Insert an event at beginning of cancellations list
  /** Inserts an event at beginning of list; increments count and sets earliest
      if applicable; sets current if list was previously empty */
  void Insert(POSE_TimeType ts, eventID e);    
  /// Return a pointer to a node in list
  /** Use current to cycle through nodes and give a different node each time
      GetItem is called */
  CancelNode *GetItem();
  /// Remove a specific cancellation from the list
  void RemoveItem(CancelNode *item);      
  /// Test if cancellations is empty
  int IsEmpty();    
  /// Return earliest timestamp
  POSE_TimeType getEarliest() { return earliest; }
  /// Dump all data fields
  void dump();               
  /// Pack/unpack/sizing operator
  void pup(PUP::er &p);                   
};

#endif
