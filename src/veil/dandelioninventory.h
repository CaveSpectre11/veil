// Copyright (c) 2018-2020 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_DANDELIONINVENTORY_H
#define VEIL_DANDELIONINVENTORY_H

#include <protocol.h>
#include <sync.h>
#include "net.h"

const uint16_t STEM_STATE_NEW        = 1; // Unassigned Peer
const uint16_t STEM_STATE_ASSIGNED   = 2; // Peer Assigned
const uint16_t STEM_STATE_NOTIFIED   = 3; // Peer Notified
const uint16_t STEM_STATE_SENT       = 4; // Tx sent to peer

class DandelionInventory;
extern DandelionInventory dandelion;

struct Stem
{
    int64_t nTimeStemEnd;
    int64_t nNodeIDFrom;
    int64_t nNodeIDTo;
    int64_t nNotifyEnd;
    int16_t nState;
};

struct DandelionRoute 
{
    int64_t expireTime;
    std::vector<int64_t> vRoutes;
};


class DandelionInventory
{
private:
    // routing constants
    const int64_t nDefaultRouteTime    = 480; // Time to persist peer routing table
    const int64_t nRouteTimeRandomizer = 240; // Randomizer to prevent constant routing
    const int16_t nPeerRouteCount      = 2;   // number of out peers for each in peer
    const int64_t nDefaultNotifyExpire = 5;   // Time to expire a notify and retry
    const int64_t nDefaultNodeID       = -1;  // Indicates the tx came from the current node

    // transaction constants
    const int64_t nDefaultStemTime     = 60;  // 60 seconds
    const int64_t nStemTimeRandomizer  = 120; // nDefaultStemTime + 0..120 seconds

    std::map<uint256, Stem> mapStemInventory;
    std::map<int64_t, DandelionRoute> mapDandelionRoutes;

    // peer routing
    bool SelectPeerRoutes(int64_t nNodeID, DandelionRoute& route);
    bool GetRoute(const int64_t nNodeID, DandelionRoute& route);
    int64_t GetPeerNode(const int64_t nNodeID);

    CCriticalSection routes;
public:
    const int64_t nStemTimeDecay      = 10;  // 10 seconds of decay per hop; hops 6-18

    bool AddNew(const uint256& hash);
    void Add(const uint256& hash, const int64_t& nTimeStemEnd, const int64_t& nNodeIDFrom);
    void DeleteFromInventory(const uint256& hash);
    bool CheckInventory(const uint256& hash) const;
    bool GetStemFromInventory(const uint256& hash, Stem &stem) const;
    bool IsInStemPhase(const uint256& hash) const;
    int64_t GetTimeStemPhaseEnd(const uint256& hash) const;
    bool IsInState(const uint256& hash, const uint16_t state) const;
    bool IsInStateAndAssigned(const uint256& hash, const uint16_t state, const int64_t nNodeID) const;
    bool IsFromNode(const uint256& hash, const int64_t nNodeID) const;
    bool IsAssignedToNode(const uint256& hash, const int64_t nNodeID);
    bool IsNodeNotified(const uint256& hash) const;
    bool SetNodeNotified(const uint256& hash, const int64_t nNodeID);
    bool IsSent(const uint256& hash) const;
    void MarkSent(const uint256& hash);
    void Process(const std::vector<CNode*>& vNodes);

    CCriticalSection stems;
};
#endif //VEIL_DANDELIONINVENTORY_H
