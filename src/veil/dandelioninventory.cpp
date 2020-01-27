// Copyright (c) 2018-2020 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <timedata.h>
#include "dandelioninventory.h"

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
    stem.nNodeIDTo = nDefaultNodeID;
    stem.nState = STEM_STATE_NEW;

    LogPrintf("(debug) %s: Adding Dandelion TX from %d; end %d: %s\n",
              __func__, nNodeIDFrom, nTimeStemEnd, hash.GetHex());
    LOCK(dandelion.cs);
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
    LOCK(dandelion.cs);
    auto it = mapStemInventory.find(hash);
    if (it == mapStemInventory.end()) {
        return false;
    }
    stem = it->second;
    return true;
}

bool DandelionInventory::IsInStemPhase(const uint256& hash) const
{
    Stem stem;
    if (!GetStemFromInventory(hash, stem))
        return false;

    return stem.nTimeStemEnd > GetAdjustedTime();
}

// Return true if hash is dandelion and assigned to the supplied node
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

bool DandelionInventory::IsAssignedToNode(const uint256& hash, const int64_t nNodeID)
{
    Stem stem;
    if (!GetStemFromInventory(hash, stem))
        return false;

    if (stem.nState != STEM_STATE_ASSIGNED) {
        LogPrintf("%s: Dandelion TX not assigned: %s\n", __func__, hash.GetHex());
        return false;
    }

    return stem.nNodeIDTo == nNodeID;
}

bool DandelionInventory::IsNodeNotified(const uint256& hash) const
{
    //If no knowledge of this hash, then assume safe to send
    if (!mapStemInventory.count(hash)) {
        // TODO: shouldn't ever be the case, and would be really bad.  Should probably assert.
        LogPrintf("%s: Dandelion TX not found: %s\n", __func__, hash.GetHex());
        return true;
    }

    return mapStemInventory.at(hash).nState == STEM_STATE_NOTIFIED;
}

bool DandelionInventory::SetNodeNotified(const uint256& hash, const int64_t nNodeID)
{
    if (!mapStemInventory.count(hash)) {
        // TODO: shouldn't ever be the case, and would be really bad.  Should probably assert.
        LogPrintf("%s: Dandelion TX not found: %s\n", __func__, hash.GetHex());
        return false;
    }

    // We might be called when trying to send to the wrong node; just tell 'em no
    if (mapStemInventory[hash].nNodeIDTo != nNodeID) {
        LogPrintf("(debug) %s: Not ours %d != %d: %s\n", __func__, mapStemInventory[hash].nNodeIDTo, nNodeID, hash.GetHex());
        return false;
    }

    LogPrintf("(debug) %s: Marking dandelion tx peer notified: %s\n", __func__, hash.GetHex());
    LOCK(dandelion.cs);
    mapStemInventory.at(hash).nState = STEM_STATE_NOTIFIED;
    return true;
}

bool DandelionInventory::IsSent(const uint256& hash) const
{
    Stem stem;
    //Assume that if it is not here, then it is sent
    if (!GetStemFromInventory(hash, stem))
        return true;

    return stem.nState == STEM_STATE_SENT;
}

void DandelionInventory::MarkSent(const uint256& hash)
{
    if (!mapStemInventory.count(hash)) {
        // TODO: shouldn't ever be the case, and would be really bad.  Should probably assert.
        LogPrintf("%s: Dandelion TX not found: %s\n", __func__, hash.GetHex());
        return;
    }

    LogPrintf("(debug) %s: Marking dandelion tx sent: %s\n", __func__, hash.GetHex());
    LOCK(dandelion.cs);
    mapStemInventory.at(hash).nState = STEM_STATE_SENT; 
}

void DandelionInventory::Process(const std::vector<CNode*>& vNodes)
{
    // Protect if there's no nodes
    if (vNodes.size() < 1) {
       LogPrintf("%s: Called with no Dandelion nodes!\n");
       return;
    }

    LOCK(dandelion.cs);
    auto mapStem = mapStemInventory;
    for (auto mi : mapStem) {
        uint256 hash = mi.first;
        Stem stem = mi.second;

        // Todo; if expired, we need to bloom it
        if (stem.nTimeStemEnd < GetAdjustedTime()) {
            mapStemInventory.erase(hash);
            LogPrintf("(debug) %s: Erasing expired dandelion tx\n", __func__);
            continue;
        }

        // Already in the system
        if (stem.nState != STEM_STATE_NEW)
            continue;

        // Set the index to send to
        int64_t nNodeID = -1;
        CNode* pNode;
        if ((vNodes.size() == 1) && (vNodes[0]->GetId() == stem.nNodeIDFrom)) {
            // Houston, we have a problem.
            LogPrintf("(debug) %s: Only node is where we got the transaction from\n", __func__);
            // let it try again later if another node comes online
            continue;
        }
        do {
            int nRand = GetRandInt(static_cast<int>(vNodes.size() - 1));
            pNode = vNodes[nRand];
            nNodeID = pNode->GetId();
            LogPrintf("(debug) %s: Selecting nNodeID %d (rand %d)\n", __func__, nNodeID, nRand);
        } while (nNodeID == stem.nNodeIDFrom);

        LogPrintf("(debug) %s: Marking dandelion tx assigned to %d: %s\n", __func__, nNodeID, hash.GetHex());
        LOCK(dandelion.cs);
        mapStemInventory.at(hash).nNodeIDTo = nNodeID;
        mapStemInventory.at(hash).nState = STEM_STATE_ASSIGNED; 

        pNode->fSendMempool = true;	// Juice it to get the mempool.
    }
}
