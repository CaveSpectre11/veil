// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <timedata.h>
#include "dandelioninventory.h"

namespace veil {

DandelionInventory dandelion;

// Return false if we didn't add the dandelion transaction; Don't add it if there aren't
// any dandelion peers.
bool DandelionInventory::AddNew(const uint256& hash)
{
    if (!g_connman || g_connman->GetDandelionNodeCount() < 1) {
        return false;
    }

    // TODO - randomize StemTime
    Add(hash, GetAdjustedTime() + nDefaultStemTime, nDefaultNodeID);
    return true;
}

void DandelionInventory::Add(const uint256& hash, const int64_t& nTimeStemEnd, const int64_t& nNodeIDFrom)
{
    Stem stem;
    stem.nTimeStemEnd = nTimeStemEnd;
    stem.nNodeIDFrom = nNodeIDFrom;

    LogPrintf("(debug) %s: Adding Dandelion TX from %d; end %d: %s\n",
              __func__, nNodeIDFrom, nTimeStemEnd, hash.GetHex());
    LOCK(veil::dandelion.cs);
    mapStemInventory.emplace(std::make_pair(hash, stem));
}

// Check if the hash was a Dandelion transaction
bool DandelionInventory::CheckInventory(const uint256& hash) const
{
    Stem stem;
    return GetStemFromInventory(hash, stem);
}

// Load the dandelion record if it's in the inventory; return false if it's not
bool DandelionInventory::GetStemFromInventory(const uint256& hash, Stem& stem) const
{
    LOCK(veil::dandelion.cs);
    auto it = mapStemInventory.find(hash);
    if (it == mapStemInventory.end()) {
        return false;
    }
    stem = it->second;
    return true;
}

int64_t DandelionInventory::GetTimeStemPhaseEnd(const uint256& hash) const
{
    Stem stem;
    if (!GetStemFromInventory(hash, stem))
        return 0;

    return stem.nTimeStemEnd;
}

bool DandelionInventory::IsFromNode(const uint256& hash, const int64_t nNodeID) const
{
    Stem stem;
    if (!GetStemFromInventory(hash, stem))
        return false;

    return stem.nNodeIDFrom == nNodeID;
}

bool DandelionInventory::IsInStemPhase(const uint256& hash) const
{
    Stem stem;
    if (!GetStemFromInventory(hash, stem))
        return false;

    return stem.nTimeStemEnd > GetAdjustedTime();
}

//Only send to a node that requests the tx if the inventory was broadcast to this node
bool DandelionInventory::IsNodePendingSend(const uint256& hash, const int64_t nNodeID)
{
    Stem stem;
    if (!GetStemFromInventory(hash, stem))
        return false;

    return stem.nNodeIDSentTo == nNodeID;
}

bool DandelionInventory::IsSent(const uint256& hash) const
{
    Stem stem;
    //Assume that if it is not here, then it is sent
    if (!GetStemFromInventory(hash, stem))
        return true;

    return stem.nNodeIDSentTo != 0;
}

void DandelionInventory::SetInventorySent(const uint256& hash, const int64_t nNodeID)
{
    if (!mapStemInventory.count(hash))
        return;
    mapStemInventory.at(hash).nNodeIDSentTo = nNodeID;
    setPendingSend.erase(hash);
    mapNodeToSentTo.erase(hash);
}

bool DandelionInventory::IsQueuedToSend(const uint256& hash) const
{
    LOCK(veil::dandelion.cs);
    //If no knowledge of this hash, then assume safe to send
    if (!mapStemInventory.count(hash))
        return true;

    return static_cast<bool>(setPendingSend.count(hash));
}

void DandelionInventory::MarkSent(const uint256& hash)
{
    LogPrintf("(debug) %s: Erasing sent dandelion tx\n", __func__);
    // Todo - retain it so we can handle if we don't see it bloomed
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

        // Todo; if expired, we need to bloom it
        if (stem.nTimeStemEnd < GetAdjustedTime()) {
            mapStemInventory.erase(mi.first);
            setPendingSend.erase(mi.first);
            LogPrintf("(debug) %s: Erasing expired dandelion tx\n", __func__);
            continue;
        }

        //Already marked this to send
        if (setPendingSend.count(mi.first))
            continue;

        //Set the index to send to
        int64_t nNodeID;
        CNode* pNode;
        do {
            int nRand = GetRandInt(static_cast<int>(vNodes.size() - 1));
            pNode = vNodes[nRand];
            nNodeID = pNode->GetId();
            LogPrintf("(debug) %s: Selecting nNodeID %d (rand %d)\n", __func__, nNodeID, nRand);
        } while (nNodeID == stem.nNodeIDFrom);

        mapNodeToSentTo.insert(std::make_pair<uint256&, int64_t& >(hash, nNodeID));

        setPendingSend.emplace(hash);
        pNode->fSendMempool = true;	// Juice it to get the mempool.
    }
}

bool DandelionInventory::IsCorrectNodeToSend(const uint256& hash, const int64_t nNodeID)
{
    return mapNodeToSentTo[hash] == nNodeID;
}

} // end namespace
