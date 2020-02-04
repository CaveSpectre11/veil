// Copyright (c) 2018-2020 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <timedata.h>
#include "dandelioninventory.h"

DandelionInventory dandelion;

bool DandelionInventory::SelectPeerRoutes(int64_t nNodeID, DandelionRoute& route)
{
    if (!g_connman) return false; // then there's no connection manager

    std::vector<CNode *>vDandelionNodes;
    int32_t nNodeCount;
    nNodeCount = g_connman->GetDandelionNodes(vDandelionNodes);

    if (!nNodeCount || (nNodeID == -1 && nNodeCount < 2)) return false; // then there's no dandelion nodes

    route.vRoutes.clear();
    // Select the routes
    for (int i=0; i<nPeerRouteCount; i++) {
        int64_t nPeerNodeID;
        std::vector<int64_t> v = route.vRoutes;
        do {
            int nRand = GetRandInt(static_cast<int>(nNodeCount - 1));
            CNode *pNode = vDandelionNodes[nRand];
            nPeerNodeID = pNode->GetId();
            LogPrintf("(debug) %s: Selecting output %d for input %d\n", __func__, nPeerNodeID, nNodeID);
        } while ((nPeerNodeID == nNodeID)           // If output same as input or
                || ((nNodeCount > nPeerRouteCount) // Enough peers and
                                                   // this one already is selected
                     && (std::find(v.begin(), v.end(), nPeerNodeID) != v.end())));
        route.vRoutes.emplace_back(nPeerNodeID);
    }

    // Set the route expire time
    route.expireTime = GetAdjustedTime() + nDefaultRouteTime + GetRandInt(nRouteTimeRandomizer);

    // update the map
    LOCK(dandelion.routes);
    mapDandelionRoutes.erase(nNodeID);
    mapDandelionRoutes.emplace(std::make_pair(nNodeID, route));
}

bool DandelionInventory::GetRoute(const int64_t nNodeID, DandelionRoute& route)
{
    auto it = mapDandelionRoutes.find(nNodeID);
    if (it != mapDandelionRoutes.end()) {
        route = it->second;
        if (route.expireTime >= GetAdjustedTime())
            return true;
    }

    // it doesn't exist or it's expired.
    return(SelectPeerRoutes(nNodeID, route));
}

int64_t DandelionInventory::GetPeerNode(const int64_t nNodeID)
{
    DandelionRoute route;

    if (!GetRoute(nNodeID, route)) {
	return -1;
    }

    return route.vRoutes[GetRandInt(static_cast<int>(route.vRoutes.size() - 1))];
}

// Return false if we didn't add the dandelion transaction; Don't add it if there aren't
// any dandelion peers.
bool DandelionInventory::AddNew(const uint256& hash)
{
    if (!g_connman || g_connman->GetDandelionNodeCount() < 1) {
        return false;
    }

    // Add will deduct StemTimeDecay
    uint64_t nStemTime = nDefaultStemTime + GetRandInt(nStemTimeRandomizer) + nStemTimeDecay;
    Add(hash, GetAdjustedTime() + nStemTime, nDefaultNodeID);
    return true;
}

void DandelionInventory::Add(const uint256& hash, const int64_t& nTimeStemEnd, const int64_t& nNodeIDFrom)
{
    Stem stem;
    stem.nTimeStemEnd = nTimeStemEnd-nStemTimeDecay;
    stem.nNodeIDFrom = nNodeIDFrom;
    stem.nNodeIDTo = nDefaultNodeID;
    stem.nState = STEM_STATE_NEW;

    LogPrintf("(debug) %s: Adding Dandelion TX from %d; end %d: %s\n",
              __func__, nNodeIDFrom, stem.nTimeStemEnd, hash.GetHex());
    LOCK(dandelion.stems);
    mapStemInventory.emplace(std::make_pair(hash, stem));
}

void DandelionInventory::DeleteFromInventory(const uint256& hash)
{
    mapStemInventory.erase(hash); // doesn't matter if it's not there
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
    LOCK(dandelion.stems);
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

bool DandelionInventory::IsInState(const uint256& hash, const uint16_t state) const
{
    Stem stem;
    if (!GetStemFromInventory(hash, stem))
        return false;

    return stem.nState == state;
}

bool DandelionInventory::IsInStateAndAssigned(const uint256& hash, const uint16_t state, const int64_t nNodeID) const
{
    Stem stem;
    if (!GetStemFromInventory(hash, stem))
        return false;

    if (stem.nState != state)
        return false;

    return stem.nNodeIDTo == nNodeID;
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
        return false;
    }

    LOCK(dandelion.stems);
    mapStemInventory.at(hash).nNotifyEnd = GetAdjustedTime() + nDefaultNotifyExpire;
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

    LOCK(dandelion.stems);
    mapStemInventory.at(hash).nState = STEM_STATE_SENT; 
}

void DandelionInventory::Process(const std::vector<CNode*>& vNodes)
{
    // Protect if there's no nodes
    if (vNodes.size() < 1) {
       return;
    }

    LOCK(dandelion.stems);
    auto mapStem = mapStemInventory;
    for (auto mi : mapStem) {
        uint256 hash = mi.first;
        Stem stem = mi.second;

        // Todo; if expired, we need to fluff it
        if (stem.nTimeStemEnd < GetAdjustedTime()) {
            mapStemInventory.erase(hash);
            LogPrintf("(debug) %s: Erasing expired dandelion tx\n", __func__);
            continue;
        }

        if (((stem.nState == STEM_STATE_NOTIFIED) || (stem.nState == STEM_STATE_ASSIGNED)) &&
                                                    (stem.nNotifyEnd <= GetAdjustedTime())) {
            LogPrintf("(debug) %s: Stem expired %d<=%d: %s\n", __func__, stem.nNotifyEnd, GetAdjustedTime(), hash.GetHex());
            stem.nNotifyEnd = 0;
            stem.nState = STEM_STATE_NEW;
            DandelionRoute route;
            SelectPeerRoutes(stem.nNodeIDFrom, route); // select new routes
        }

        // Already in the system
        if (stem.nState != STEM_STATE_NEW)
            continue;

        int64_t nPeerNodeID = GetPeerNode(stem.nNodeIDFrom);
        static bool peerFailureReported = false;
        if (nPeerNodeID == -1) {
            if (!peerFailureReported) {
                // Report that we're waiting for more peers
                LogPrintf("Notice: Not enough dandelion peers.  Waiting for more\n", __func__);
            }
            peerFailureReported = true;
            // let it try again later
            continue;
        }
        peerFailureReported = false;

        LogPrintf("(debug) %s: Routing dandelion tx from %d to %d: %s\n",
                   __func__, stem.nNodeIDFrom, nPeerNodeID, hash.GetHex());
        LOCK(dandelion.stems);
        mapStemInventory.at(hash).nNodeIDTo = nPeerNodeID;
        // Note: If nNotifyEnd is after the expire time, doesn't matter, we'll fluff.
        mapStemInventory.at(hash).nNotifyEnd = GetAdjustedTime() + nDefaultNotifyExpire;
        mapStemInventory.at(hash).nState = STEM_STATE_ASSIGNED; 

	// Juice the peer to get the mempool.
        g_connman->SetSendMempool(nPeerNodeID);
    }
}
