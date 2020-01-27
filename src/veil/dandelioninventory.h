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
    int64_t nState;
};

class DandelionInventory
{
private:
    std::map<uint256, Stem> mapStemInventory;
public:
    const int64_t nDefaultStemTime    = 60;  // 60 seconds
    const int64_t nStemTimeRandomizer = 120; // nDefaultStemTime + 0..120 seconds
    const int64_t nStemTimeDecay      = 10;  // 10 seconds of decay per hop; hops 6-18
    const int64_t nDefaultNodeID      = -1;  // Indicates the tx came from the current node

    bool AddNew(const uint256& hash);
    void Add(const uint256& hash, const int64_t& nTimeStemEnd, const int64_t& nNodeIDFrom);
    bool CheckInventory(const uint256& hash) const;
    bool GetStemFromInventory(const uint256& hash, Stem &stem) const;
    bool IsInStemPhase(const uint256& hash) const;
    int64_t GetTimeStemPhaseEnd(const uint256& hash) const;
    bool IsFromNode(const uint256& hash, const int64_t nNodeID) const;
    bool IsAssignedToNode(const uint256& hash, const int64_t nNodeID);
    bool IsNodeNotified(const uint256& hash) const;
    bool SetNodeNotified(const uint256& hash, const int64_t nNodeID);
    bool IsSent(const uint256& hash) const;
    void MarkSent(const uint256& hash);
    void Process(const std::vector<CNode*>& vNodes);

    CCriticalSection cs;
};
#endif //VEIL_DANDELIONINVENTORY_H
