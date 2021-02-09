// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "activemasternode.h"
#include "masternode-sync.h"
#include "masternode-payments.h"
#include "masternode.h"
#include "masternodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CMasternodeSync;
CMasternodeSync masternodeSync;

CMasternodeSync::CMasternodeSync()
{
    Reset();
}

bool CMasternodeSync::IsSynced()
{
    return RequestedMasternodeAssets == MASTERNODE_SYNC_FINISHED;
}

bool CMasternodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }

    lastProcess = GetTime();

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();

    if (!pindex) return false;

    if (pindex->nTime + 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CMasternodeSync::Reset()
{
    lastMasternodeList = 0;
    lastMasternodeWinner = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumMasternodeList = 0;
    sumMasternodeWinner = 0;
    countMasternodeList = 0;
    countMasternodeWinner = 0;
    RequestedMasternodeAssets = MASTERNODE_SYNC_INITIAL;
    RequestedMasternodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CMasternodeSync::AddedMasternodeList(uint256 hash)
{
    auto ins_res = mapSeenSyncMNB.emplace(hash, 1);

    if(!ins_res.second) {

        auto& seen_sync_mnb = ins_res.first->second;

        if(seen_sync_mnb >= MASTERNODE_SYNC_THRESHOLD)
            return;

        ++seen_sync_mnb;
    }

    lastMasternodeList = GetTime();

/*
    if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastMasternodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastMasternodeList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
*/
}

void CMasternodeSync::AddedMasternodeWinner(uint256 hash)
{

    auto ins_res = mapSeenSyncMNW.emplace(hash, 1);

    if(!ins_res.second) {

        auto& seen_sync_mnw = ins_res.first->second;

        if(seen_sync_mnw >= MASTERNODE_SYNC_THRESHOLD)
            return;

        ++seen_sync_mnw;
    }

    lastMasternodeWinner = GetTime();

/*
    if (masternodePayments.mapMasternodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastMasternodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastMasternodeWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
*/
}

void CMasternodeSync::GetNextAsset()
{
    switch (RequestedMasternodeAssets) {
        case (MASTERNODE_SYNC_INITIAL):
        case (MASTERNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
            ClearFulfilledRequest();
            RequestedMasternodeAssets = MASTERNODE_SYNC_SPORKS;
            break;
        case (MASTERNODE_SYNC_SPORKS):
            RequestedMasternodeAssets = MASTERNODE_SYNC_LIST;
            break;
        case (MASTERNODE_SYNC_LIST):
            RequestedMasternodeAssets = MASTERNODE_SYNC_MNW;
            break;
        case (MASTERNODE_SYNC_MNW):
            RequestedMasternodeAssets = MASTERNODE_SYNC_GM;
            break;
        case (MASTERNODE_SYNC_GM):
            LogPrintf("CMasternodeSync::GetNextAsset - Sync has finished\n");
            RequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
            if (!txFilterState) BuildTxFilter();
            break;
    }
    RequestedMasternodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CMasternodeSync::GetSyncStatus()
{
    switch (masternodeSync.RequestedMasternodeAssets) {
        case MASTERNODE_SYNC_INITIAL:
            return _("Synchronization pending...");
        case MASTERNODE_SYNC_SPORKS:
            return _("Synchronizing sporks...");
        case MASTERNODE_SYNC_LIST:
            return _("Synchronizing masternodes...");
        case MASTERNODE_SYNC_MNW:
            return _("Synchronizing masternode winners...");
        case MASTERNODE_SYNC_FAILED:
            return _("Synchronization failed");
        case MASTERNODE_SYNC_FINISHED:
            return _("Synchronization finished");
        case MASTERNODE_SYNC_GM:
            return _("Synchronizing FDR data...");
    }
    return "";
}

void CMasternodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedMasternodeAssets >= MASTERNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
            case (MASTERNODE_SYNC_LIST):
                if (nItemID != RequestedMasternodeAssets) return;
                sumMasternodeList += nCount;
                countMasternodeList++;
                break;
            case (MASTERNODE_SYNC_MNW):
                if (nItemID != RequestedMasternodeAssets) return;
                sumMasternodeWinner += nCount;
                countMasternodeWinner++;
                break;
        }

        LogPrint("masternode", "CMasternodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CMasternodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    for(CNode* pnode : vNodes) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("mnsync");
        pnode->ClearFulfilledRequest("mnwsync");
    }
}

void CMasternodeSync::Process()
{
    static int tick = 0;

    if (tick++ % MASTERNODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /*
            Resync if we lose all masternodes from sleep/wake or failure to sync originally
        */
        if (IsSporkActive(SPORK_4_MASTERNODE_PAYMENT_ENFORCEMENT) && !mnodeman.CountEnabled()) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED && lastFailure + (1 * 10) < GetTime()) {
        Reset();
    } else if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED) {
        return;
    }

    LogPrint("masternode", "CMasternodeSync::Process() - tick %d RequestedMasternodeAssets %d\n", tick, RequestedMasternodeAssets);

    if (RequestedMasternodeAssets == MASTERNODE_SYNC_INITIAL)
        GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    bool wait_blockchain_sync =  Params().NetworkID() != CBaseChainParams::REGTEST
                              && !IsBlockchainSynced()
                              && RequestedMasternodeAssets > MASTERNODE_SYNC_SPORKS;

    if(wait_blockchain_sync)
    {
        nAssetSyncStarted = GetTime();
        return;
    }

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    for(CNode* pnode : vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (RequestedMasternodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if (RequestedMasternodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (RequestedMasternodeAttempt < 6) {
                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
            } else {
                RequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
            }
            RequestedMasternodeAttempt++;
            return;
        }

        //set to synced
        if (RequestedMasternodeAssets == MASTERNODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork"))
                continue;

            if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD) {
                GetNextAsset();
                return;
            }

            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            RequestedMasternodeAttempt++;
            return;
        }

        //sync gm data
        if (RequestedMasternodeAssets == MASTERNODE_SYNC_GM) {
            if (pnode->HasFulfilledRequest("getgm"))
                continue;
            if (RequestedMasternodeAttempt >= 2) {
                GetNextAsset();
                return;
            }
            pnode->FulfilledRequest("getgm");
            pnode->PushMessage("getgm"); //request gm data
            RequestedMasternodeAttempt++;
            return;
        }

        if (pnode->nVersion >= masternodePayments.GetMinMasternodePaymentsProto()) {
            if (RequestedMasternodeAssets == MASTERNODE_SYNC_LIST) {
                LogPrint("masternode", "CMasternodeSync::Process() - lastMasternodeList %lld (GetTime() - MASTERNODE_SYNC_TIMEOUT) %lld\n", lastMasternodeList, GetTime() - MASTERNODE_SYNC_TIMEOUT);
                if (lastMasternodeList > 0 && lastMasternodeList < GetTime() - MASTERNODE_SYNC_TIMEOUT && RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnsync"))
                    continue;

                pnode->FulfilledRequest("mnsync");

                // timeout
                LogPrint("masternode", "CMasternodeSync::Process() - CheckTimeout: lastMasternodeList=%lld RequestedMasternodeAttempt=%lld GetTime() - nAssetSyncStarted=%lld\n", lastMasternodeList, RequestedMasternodeAttempt, GetTime() - nAssetSyncStarted);
                LogPrint("masternode", "CMasternodeSync::Process() - mnodeman.CountEnabled()=%lld\n", mnodeman.CountEnabled());
                if (lastMasternodeList == 0 &&
                    (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_4_MASTERNODE_PAYMENT_ENFORCEMENT) && mnodeman.CountEnabled() > 6) {
                        LogPrintf("CMasternodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
                        RequestedMasternodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3)
                    return;

                if(!mnodeman.DsegUpdate(pnode))
                    continue;

                ++RequestedMasternodeAttempt;

                return;
            }

            if (RequestedMasternodeAssets == MASTERNODE_SYNC_MNW) {

                if ((lastMasternodeWinner > 0 || countMasternodeWinner >= MASTERNODE_SYNC_THRESHOLD) && RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD) {
                    GetNextAsset();
                    // Try to activate our masternode if possible
                    activeMasternode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnwsync"))
                    continue;

                pnode->FulfilledRequest("mnwsync");

                // timeout
                LogPrint("masternode", "CMasternodeSync::Process() - CheckTimeout: lastMasternodeWinner=%lld RequestedMasternodeAttempt=%lld GetTime() - nAssetSyncStarted=%lld\n", lastMasternodeWinner, RequestedMasternodeAttempt, GetTime() - nAssetSyncStarted);
                LogPrint("masternode", "CMasternodeSync::Process() - mnodeman.CountEnabled()=%lld\n", mnodeman.CountEnabled());
                if (lastMasternodeWinner == 0 &&
                    (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_4_MASTERNODE_PAYMENT_ENFORCEMENT) && mnodeman.CountEnabled() > 6) {
                        LogPrintf("CMasternodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedMasternodeAssets = MASTERNODE_SYNC_FAILED;
                        RequestedMasternodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                        // Try to activate our masternode if possible
                        activeMasternode.ManageStatus();
                    }
                    return;
                }

                if (RequestedMasternodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3)
                    return;

                if (!chainActive.Tip())
                    return;

                if(!mnodeman.WinnersUpdate(pnode))
                    continue;

                ++RequestedMasternodeAttempt;

                return;
            }
        }
    }
}
