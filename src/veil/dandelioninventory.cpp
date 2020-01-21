// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <timedata.h>
#include "dandelioninventory.h"

namespace veil {

DandelionInventory dandelion;

// Return false if we didn't add the dandelion transaction; Don't add it if there aren't
// any dandelion peers.
bool DandelionInventory::AddNew(const uint256& hashInventory)
{
    if (!fEnableDandelion) return false;

    if (!g_connman || g_connman->GetDandelionNodeCount() < 1) {
        LogPrintf("%s: No Dandelion peers found\n", __func__);
        return false;
    }

    // TODO - randomize StemTime
    Add(hashInventory, GetAdjustedTime() + nDefaultStemTime, nDefaultNodeID);
    return true;
}

void DandelionInventory::Add(const uint256& hashInventory, const int64_t& nTimeStemEnd, const int64_t& nNodeIDFrom)
{
    Stem stem;
    stem.nTimeStemEnd = nTimeStemEnd;
    stem.nNodeIDFrom = nNodeIDFrom;

    LOCK(veil::dandelion.cs);
    mapStemInventory.emplace(std::make_pair(hashInventory, stem));
}

int64_t DandelionInventory::GetTimeStemPhaseEnd(const uint256& hashObject) const
{
    LOCK(veil::dandelion.cs);
    if (!mapStemInventory.count(hashObject))
        return 0;

    return mapStemInventory.at(hashObject).nTimeStemEnd;
}

bool DandelionInventory::IsFromNode(const uint256& hash, const int64_t nNodeID) const
{
    LOCK(veil::dandelion.cs);
    if (!mapStemInventory.count(hash))
        return false;

    return mapStemInventory.at(hash).nNodeIDFrom == nNodeID;
}

bool DandelionInventory::IsInStemPhase(const uint256& hash) const
{
    LOCK(veil::dandelion.cs);
    if (!mapStemInventory.count(hash))
        return false;

    return mapStemInventory.at(hash).nTimeStemEnd < GetAdjustedTime();
}

//Only send to a node that requests the tx if the inventory was broadcast to this node
bool DandelionInventory::IsNodePendingSend(const uint256& hashInventory, const int64_t nNodeID)
{
    LOCK(veil::dandelion.cs);
    if (!mapStemInventory.count(hashInventory))
        return true;

    return mapStemInventory.at(hashInventory).nNodeIDSentTo == nNodeID;
}

bool DandelionInventory::IsSent(const uint256& hash) const
{
    LOCK(veil::dandelion.cs);
    //Assume that if it is not here, then it is sent
    if (!mapStemInventory.count(hash))
        return true;

    return mapStemInventory.at(hash).nNodeIDSentTo != 0;
}

void DandelionInventory::SetInventorySent(const uint256& hash, const int64_t nNodeID)
{
    LOCK(veil::dandelion.cs);
    if (!mapStemInventory.count(hash))
        return;
    mapStemInventory.at(hash).nNodeIDSentTo = nNodeID;
    setPendingSend.erase(hash);
    mapNodeToSentTo.erase(hash);
}

bool DandelionInventory::IsQueuedToSend(const uint256& hashObject) const
{
    LOCK(veil::dandelion.cs);
    //If no knowledge of this hash, then assume safe to send
    if (!mapStemInventory.count(hashObject))
        return true;

    return static_cast<bool>(setPendingSend.count(hashObject));
}

void DandelionInventory::MarkSent(const uint256& hash)
{
    LOCK(veil::dandelion.cs);
    mapStemInventory.erase(hash);
    setPendingSend.erase(hash);
    mapNodeToSentTo.erase(hash);
}

void DandelionInventory::Process(const std::vector<CNode*>& vNodes)
{
    // Protect if there's no nodes
    if (vNodes.size() < 1) {
       LogPrintf("%s: Called with no Dandelion nodes!\n");
       return;
    }

    //Clear all the old node destinations
    mapNodeToSentTo.clear();
    LOCK(veil::dandelion.cs);
    auto mapStem = mapStemInventory;
    for (auto mi : mapStem) {
        auto hash = mi.first;
        auto stem = mi.second;

        //If in the fluff phase, remove from this tracker
        if (stem.nTimeStemEnd < GetAdjustedTime()) {
            mapStemInventory.erase(mi.first);
            setPendingSend.erase(mi.first);
            continue;
        }

        //Already marked this to send
        if (setPendingSend.count(mi.first))
            continue;

        //If rolled recently, then wait
        if (GetAdjustedTime() - stem.nTimeLastRoll < 5)
            continue;
        mapStemInventory.at(hash).nTimeLastRoll = GetAdjustedTime();

        //Set the index to send to
        int64_t nNodeID;
        do {
            int nRand = GetRandInt(static_cast<int>(vNodes.size() - 1));
            nNodeID = vNodes[nRand]->GetId();
        }
        while (nNodeID == stem.nNodeIDFrom);
        mapNodeToSentTo.insert(std::make_pair<uint256&, int64_t& >(hash, nNodeID));

        // Randomly decide to send this if it is in stem phase
        auto n = GetRandInt(3);
        if (n == 1)
            setPendingSend.emplace(hash);
    }
}

    bool DandelionInventory::IsCorrectNodeToSend(const uint256& hash, const int64_t nNodeID)
    {
        return mapNodeToSentTo[hash] == nNodeID;
    }

}

