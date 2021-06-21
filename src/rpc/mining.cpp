// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "amount.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/funding.h"
#include "consensus/validation.h"
#include "core_io.h"
#ifdef ENABLE_MINING
#include "crypto/equihash.h"
#endif
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "metrics.h"
#include "miner.h"
#include "net.h"
#include "pow.h"
#include "rpc/server.h"
#include "txmempool.h"
#include "util.h"
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif
#include "rpc/docstrings.h"

#include <stdint.h>
#include <variant>

#include <boost/assign/list_of.hpp>
#include <boost/shared_ptr.hpp>

#include <univalue.h>

using namespace std;

/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or over the difficulty averaging window if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
int64_t GetNetworkHashPS(int lookup, int height)
{
    CBlockIndex* pb = chainActive.Tip();

    if (height >= 0 && height < chainActive.Height())
        pb = chainActive[height];

    if (pb == NULL || !pb->nHeight)
        return 0;

    // If lookup is nonpositive, then use difficulty averaging window.
    if (lookup <= 0)
        lookup = Params().GetConsensus().nPowAveragingWindow;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex* pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return (int64_t)(workDiff.getdouble() / timeDiff);
}

UniValue getlocalsolps(const UniValue& params, bool fHelp)
{
    if (fHelp) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_description("Returns the average local solutions per second since this node was started.\n"
                                 "This is the same information shown on the metrics screen (if enabled).")
                .set_result("xxx.xxxxx     (numeric) Solutions per second average");
        throw runtime_error(
            help_sections.combine_sections());
    }
    LOCK(cs_main);
    return GetLocalSolPS();
}

UniValue getnetworksolps(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("( blocks height )")
                .set_description("Returns the estimated network solutions per second based on the last n blocks.\n"
                                 "Pass in [blocks] to override # of blocks, -1 specifies over difficulty averaging window.\n"
                                 "Pass in [height] to estimate the network speed at the time when a certain block was found.")
                .set_arguments("1. blocks     (numeric, optional, default=120) The number of blocks, or -1 for blocks over difficulty averaging window.\n"
                               "2. height     (numeric, optional, default=-1) To estimate at the time of the given height.")
                .set_result("x             (numeric) Solutions per second estimated");
        throw runtime_error(
            help_sections.combine_sections());
    }

    LOCK(cs_main);
    return GetNetworkHashPS(params.size() > 0 ? params[0].get_int() : 120, params.size() > 1 ? params[1].get_int() : -1);
}

UniValue getnetworkhashps(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("( blocks height )")
                .set_description("DEPRECATED - left for backwards-compatibility. Use getnetworksolps instead.\n"
                                 "\nReturns the estimated network solutions per second based on the last n blocks.\n"
                                 "Pass in [blocks] to override # of blocks, -1 specifies over difficulty averaging window.\n"
                                 "Pass in [height] to estimate the network speed at the time when a certain block was found.")
                .set_arguments("1. blocks     (numeric, optional, default=120) The number of blocks, or -1 for blocks over difficulty averaging window.\n"
                               "2. height     (numeric, optional, default=-1) To estimate at the time of the given height.")
                .set_result("x             (numeric) Solutions per second estimated");
        throw runtime_error(
            help_sections.combine_sections());
    }

    LOCK(cs_main);
    return GetNetworkHashPS(params.size() > 0 ? params[0].get_int() : 120, params.size() > 1 ? params[1].get_int() : -1);
}

#ifdef ENABLE_MINING
UniValue getgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_description("Return if the server is set to generate coins or not. The default is false.\n"
                                 "It is set with the command line argument -gen (or " +
                                 std::string(BITCOIN_CONF_FILENAME) + " setting gen)\n"
                                                                      "It can also be set with the setgenerate call.")
                .set_result("true|false    (boolean) If the server is set to generate coins or not");
        throw runtime_error(help_sections.combine_sections());
    }

    LOCK(cs_main);
    return GetBoolArg("-gen", DEFAULT_GENERATE);
}

UniValue generate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("numblocks")
                .set_description("Mine blocks immediately (before the RPC call returns)\n"
                                 "\nNote: this function can only be used on the regtest network")
                .set_arguments("1. numblocks    (numeric, required) How many blocks are generated immediately.")
                .set_result("[\n"
                            "  \"blockhashes\"     (string) hashes of blocks generated\n"
                            "  , ...\n"
                            "]")
                .set_examples("11", "Generate 11 blocks");
        throw runtime_error(
            help_sections.combine_sections());
    }

    if (!Params().MineBlocksOnDemand())
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This method can only be used on regtest");

    int nHeightStart = 0;
    int nHeightEnd = 0;
    int nHeight = 0;
    int nGenerate = params[0].get_int();

    MinerAddress minerAddress;
    GetMainSignals().AddressForMining(minerAddress);

    // If the keypool is exhausted, no script is returned at all.  Catch this.
    auto resv = std::get_if<boost::shared_ptr<CReserveScript>>(&minerAddress);
    if (resv && !resv->get()) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }

    // Throw an error if no address valid for mining was provided.
    if (!std::visit(IsValidMinerAddress(), minerAddress)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No miner address available (mining requires a wallet or -mineraddress)");
    }

    { // Don't keep cs_main locked
        LOCK(cs_main);
        nHeightStart = chainActive.Height();
        nHeight = nHeightStart;
        nHeightEnd = nHeightStart + nGenerate;
    }
    unsigned int nExtraNonce = 0;
    UniValue blockHashes(UniValue::VARR);
    unsigned int n = Params().GetConsensus().nEquihashN;
    unsigned int k = Params().GetConsensus().nEquihashK;
    while (nHeight < nHeightEnd) {
        std::unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlock(Params(), minerAddress));
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");
        CBlock* pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            IncrementExtraNonce(pblocktemplate.get(), chainActive.Tip(), nExtraNonce, Params().GetConsensus());
        }

        // Hash state
        eh_HashState eh_state;
        EhInitialiseState(n, k, eh_state);

        // I = the block header minus nonce and solution.
        CEquihashInput I{*pblock};
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << I;

        // H(I||...
        eh_state.Update((unsigned char*)&ss[0], ss.size());

        while (true) {
            // Yes, there is a chance every nonce could fail to satisfy the -regtest
            // target -- 1 in 2^(2^256). That ain't gonna happen
            pblock->nNonce = ArithToUint256(UintToArith256(pblock->nNonce) + 1);

            // H(I||V||...
            eh_HashState curr_state(eh_state);
            curr_state.Update(pblock->nNonce.begin(), pblock->nNonce.size());

            // (x_1, x_2, ...) = A(I, V, n, k)
            std::function<bool(std::vector<unsigned char>)> validBlock =
                [&pblock](std::vector<unsigned char> soln) {
                    pblock->nSolution = soln;
                    solutionTargetChecks.increment();
                    return CheckProofOfWork(pblock->GetHash(), pblock->nBits, Params().GetConsensus());
                };
            bool found = EhBasicSolveUncancellable(n, k, curr_state, validBlock);
            ehSolverRuns.increment();
            if (found) {
                goto endloop;
            }
        }
    endloop:
        CValidationState state;
        if (!ProcessNewBlock(state, Params(), NULL, pblock, true, NULL))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        //mark miner address as important because it was used at least for one coinbase output
        std::visit(KeepMinerAddress(), minerAddress);
    }
    return blockHashes;
}

UniValue setgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("generate ( genproclimit )")
                .set_description("Set 'generate' true or false to turn generation on or off.\n"
                                 "Generation is limited to 'genproclimit' processors, -1 is unlimited.\n"
                                 "See the getgenerate call for the current setting.")
                .set_arguments("1. generate         (boolean, required) Set to true to turn on generation, off to turn off.\n"
                               "2. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for unlimited.")
                .set_examples("true 1", "Set the generation on with a limit of one processor")
                .set_examples("", "Check the setting")
                .set_examples("false", "Turn off generation");
        throw runtime_error(help_sections.combine_sections());
    }

    if (Params().MineBlocksOnDemand())
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Use the generate method instead of setgenerate on this network");

    bool fGenerate = true;
    if (params.size() > 0)
        fGenerate = params[0].get_bool();

    int nGenProcLimit = GetArg("-genproclimit", DEFAULT_GENERATE_THREADS);
    if (params.size() > 1) {
        nGenProcLimit = params[1].get_int();
        if (nGenProcLimit == 0)
            fGenerate = false;
    }

    mapArgs["-gen"] = (fGenerate ? "1" : "0");
    mapArgs["-genproclimit"] = itostr(nGenProcLimit);
    GenerateBitcoins(fGenerate, nGenProcLimit, Params());

    return NullUniValue;
}
#endif

UniValue getmininginfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_description("Returns a json object containing mining-related information.")
                .set_result("{\n"
                            "  \"blocks\": nnn,             (numeric) The current block\n"
                            "  \"currentblocksize\": nnn,   (numeric) The last block size\n"
                            "  \"currentblocktx\": nnn,     (numeric) The last block transaction\n"
                            "  \"difficulty\": xxx.xxxxx    (numeric) The current difficulty\n"
                            "  \"errors\": \"...\"          (string) Current errors\n"
                            "  \"generate\": true|false     (boolean) If the generation is on or off (see getgenerate or setgenerate calls)\n"
                            "  \"genproclimit\": n          (numeric) The processor limit for generation. -1 if no generation. (see getgenerate or setgenerate calls)\n"
                            "  \"localsolps\": xxx.xxxxx    (numeric) The average local solution rate in Sol/s since this node was started\n"
                            "  \"networksolps\": x          (numeric) The estimated network solution rate in Sol/s\n"
                            "  \"pooledtx\": n              (numeric) The size of the mem pool\n"
                            "  \"testnet\": true|false      (boolean) If using testnet or not\n"
                            "  \"chain\": \"xxxx\",         (string) current network name as defined in BIP70 (main, test, regtest)\n"
                            "}");
        throw runtime_error(
            help_sections.combine_sections());
    }

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks", (int)chainActive.Height());
    obj.pushKV("currentblocksize", (uint64_t)nLastBlockSize);
    obj.pushKV("currentblocktx", (uint64_t)nLastBlockTx);
    obj.pushKV("difficulty", (double)GetNetworkDifficulty());
    auto warnings = GetWarnings("statusbar");
    obj.pushKV("errors", warnings.first);
    obj.pushKV("errorstimestamp", warnings.second);
    obj.pushKV("genproclimit", (int)GetArg("-genproclimit", DEFAULT_GENERATE_THREADS));
    obj.pushKV("localsolps", getlocalsolps(params, false));
    obj.pushKV("networksolps", getnetworksolps(params, false));
    obj.pushKV("networkhashps", getnetworksolps(params, false));
    obj.pushKV("pooledtx", (uint64_t)mempool.size());
    obj.pushKV("testnet", Params().TestnetToBeDeprecatedFieldRPC());
    obj.pushKV("chain", Params().NetworkIDString());
#ifdef ENABLE_MINING
    obj.pushKV("generate", getgenerate(params, false));
#endif
    return obj;
}


// NOTE: Unlike wallet RPC (which use BTC values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
UniValue prioritisetransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("<txid> <priority delta> <fee delta>")
                .set_description("Accepts the transaction into mined blocks at a higher (or lower) priority")
                .set_arguments("1. \"txid\"       (string, required) The transaction id.\n"
                               "2. priority delta (numeric, required) The priority to add or subtract.\n"
                               "                  The transaction selection algorithm considers the tx as it would have a higher priority.\n"
                               "                  (priority of a transaction is calculated: coinage * value_in_satoshis / txsize) \n"
                               "3. fee delta      (numeric, required) The fee value (in satoshis) to add (or subtract, if negative).\n"
                               "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
                               "                  considers the transaction as it would have paid a higher (or lower) fee.")
                .set_result("true              (boolean) Returns true")
                .set_examples("\"txid\" 0.0 10000");
        throw runtime_error(
            help_sections.combine_sections());
    }

    LOCK(cs_main);

    uint256 hash = ParseHashStr(params[0].get_str(), "txid");
    CAmount nAmount = params[2].get_int64();

    mempool.PrioritiseTransaction(hash, params[0].get_str(), params[1].get_real(), nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    std::string strRejectReason = state.GetRejectReason();
    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    if (state.IsInvalid()) {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

UniValue getblocktemplate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("( \"jsonrequestobject\" )")
                .set_description("If the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
                                 "It returns data needed to construct a block to work on.\n"
                                 "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

                                 "\nTo obtain information about founder's reward or funding stream\n"
                                 "amounts, use 'getblocksubsidy HEIGHT' passing in the height returned\n"
                                 "by this API.")
                .set_arguments("1. \"jsonrequestobject\"       (string, optional) A json object in the following spec\n"
                               "     {\n"
                               "       \"mode\":\"template,\"    (string, optional) This must be set to \"template\" or omitted\n"
                               "       \"capabilities\":[      (array, optional) A list of strings\n"
                               "           \"support\"         (string) client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'\n"
                               "           ,...\n"
                               "         ],\n"
                               "       \"longpollid\":\"id\"     (string, optional) id to wait for\n"
                               "     }")
                .set_result("{\n"
                            "  \"version\" : n,                     (numeric) The block version\n"
                            "  \"previousblockhash\" : \"xxxx\",      (string) The hash of current highest block\n"
                            "  \"lightclientroothash\" : \"xxxx\",    (string) The hash of the light client root field in the block header\n"
                            "  \"finalsaplingroothash\" : \"xxxx\",   (string) (DEPRECATED) The hash of the light client root field in the block header\n"
                            "  \"transactions\" : [                 (array) contents of non-coinbase transactions that should be included in the next block\n"
                            "      {\n"
                            "         \"data\" : \"xxxx\",            (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
                            "         \"hash\" : \"xxxx\",            (string) hash/id encoded in little-endian hexadecimal\n"
                            "         \"depends\" : [               (array) array of numbers \n"
                            "             n                       (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is\n"
                            "             ,...\n"
                            "         ],\n"
                            "         \"fee\": n,                   (numeric) difference in value between transaction inputs and outputs (in Satoshis); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one\n"
                            "         \"sigops\" : n,               (numeric) total number of SigOps, as counted for purposes of block limits; if key is not present, sigop count is unknown and clients MUST NOT assume there aren't any\n"
                            "         \"required\" : true|false     (boolean) if provided and true, this transaction must be in the final block\n"
                            "      }\n"
                            "      ,...\n"
                            "  ],\n"
                            //            "  \"coinbaseaux\" : {                  (json object) data that should be included in the coinbase's scriptSig content\n"
                            //            "      \"flags\" : \"flags\"            (string) \n"
                            //            "  },\n"
                            //            "  \"coinbasevalue\" : n,               (numeric) maximum allowable input to coinbase transaction, including the generation award and transaction fees (in Satoshis)\n"
                            "  \"coinbasetxn\" : {                  (json object) information for coinbase transaction\n"
                            "    \"data\":    (hexadecimal)\n"
                            "    \"hash\":    (hexadecimal)\n"
                            "    \"depends\":    [\n"
                            "         (numeric)\n"
                            "    ]\n"
                            "    \"fee\":    (numeric)\n"
                            "    \"foundersreward\":    (numeric)\n"
                            "    \"sigops\":    (numeric)\n"
                            "    \"required\":    (boolean)\n"
                            "  },\n"
                            "  \"target\" : \"xxxx\",                 (string) The hash target\n"
                            "  \"longpollid\" : \"str\",              (string) an id to include with a request to longpoll on an update to this template\n"
                            "  \"mintime\" : xxx,                   (numeric) The minimum timestamp appropriate for next block time in seconds since epoch (Jan 1 1970 GMT)\n"
                            "  \"mutable\" : [                      (array of string) list of ways the block template may be changed \n"
                            "     \"value\"                         (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'\n"
                            "     ,...\n"
                            "  ],\n"
                            "  \"noncerange\" : \"00000000ffffffff\", (string) A range of valid nonces\n"
                            "  \"sigoplimit\" : n,                  (numeric) limit of sigops in blocks\n"
                            "  \"sizelimit\" : n,                   (numeric) limit of block size\n"
                            "  \"curtime\" : ttt,                   (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
                            "  \"bits\" : \"xxx\",                    (string) compressed target of next block\n"
                            "  \"height\" : n                       (numeric) The height of the next block\n"
                            "}");
        throw runtime_error(
            help_sections.combine_sections());
    }

    LOCK(cs_main);

    // Wallet or miner address is required because we support coinbasetxn
    if (GetArg("-mineraddress", "").empty()) {
#ifdef ENABLE_WALLET
        if (!pwalletMain) {
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Wallet disabled and -mineraddress not set");
        }
#else
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "zcashd compiled without wallet and -mineraddress not set");
#endif
    }

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    // TODO: Re-enable coinbasevalue once a specification has been written
    bool coinbasetxn = true;
    if (params.size() > 0) {
        const UniValue& oparam = params[0].get_obj();
        const UniValue& modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull()) {
            /* Do nothing */
        } else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        if (strMode == "proposal") {
            const UniValue& dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            BlockMap::iterator mi = mapBlockIndex.find(hash);
            if (mi != mapBlockIndex.end()) {
                CBlockIndex* pindex = mi->second;
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = chainActive.Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";

            CValidationState state;
            TestBlockValidity(state, Params(), block, pindexPrev, true);
            return BIP22ValidationResult(state);
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if (Params().NetworkIDString() != "regtest" && vNodes.empty())
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Zcash is not connected!");

    if (IsInitialBlockDownload(Params().GetConsensus()))
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Zcash is downloading blocks...");


    MinerAddress minerAddress;
    GetMainSignals().AddressForMining(minerAddress);

    static unsigned int nTransactionsUpdatedLast;
    static std::optional<CMutableTransaction> cached_next_cb_mtx;
    static int cached_next_cb_height;

    // Use the cached shielded coinbase only if the height hasn't changed.
    const int nHeight = chainActive.Tip()->nHeight;
    if (cached_next_cb_height != nHeight + 2) {
        cached_next_cb_mtx = nullopt;
    }

    std::optional<CMutableTransaction> next_cb_mtx(cached_next_cb_mtx);

    if (!lpval.isNull()) {
        // Wait to respond until either the best block changes, OR some time passes and there are more transactions
        uint256 hashWatchedChain;
        boost::system_time checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr()) {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain.SetHex(lpstr.substr(0, 64));
            nTransactionsUpdatedLastLP = atoi64(lpstr.substr(64));
        } else {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = chainActive.Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        {
            checktxtime = boost::get_system_time() + boost::posix_time::seconds(10);

            boost::unique_lock<boost::mutex> lock(csBestBlock);
            while (chainActive.Tip()->GetBlockHash() == hashWatchedChain && IsRPCRunning()) {
                // Release the main lock while waiting
                LEAVE_CRITICAL_SECTION(cs_main);

                // Before waiting, generate the coinbase for the block following the next
                // block (since this is cpu-intensive), so that when next block arrives,
                // we can quickly respond with a template for following block.
                // Note that the time to create the coinbase tx here does not add to,
                // but instead is included in, the 10 second delay, since we're waiting
                // until an absolute time is reached.
                if (!cached_next_cb_mtx && IsShieldedMinerAddress(minerAddress)) {
                    cached_next_cb_height = nHeight + 2;
                    cached_next_cb_mtx = CreateCoinbaseTransaction(
                        Params(), CAmount{0}, minerAddress, cached_next_cb_height);
                    next_cb_mtx = cached_next_cb_mtx;
                }
                bool timedout = !cvBlockChange.timed_wait(lock, checktxtime);
                ENTER_CRITICAL_SECTION(cs_main);

                // Optimization: even if timed out, a new block may have arrived
                // while waiting for cs_main; if so, don't discard next_cb_mtx.
                if (chainActive.Tip()->GetBlockHash() != hashWatchedChain)
                    break;

                // Timeout: Check transactions for update
                if (timedout && mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP) {
                    // Create a non-empty block.
                    next_cb_mtx = nullopt;
                    break;
                }
                checktxtime += boost::posix_time::seconds(10);
            }
            if (chainActive.Tip()->nHeight != nHeight + 1) {
                // Unexpected height (reorg or >1 blocks arrived while waiting) invalidates coinbase tx.
                next_cb_mtx = nullopt;
            }
        }

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static CBlockTemplate* pblocktemplate;
    if (!lpval.isNull() || pindexPrev != chainActive.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5)) {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();

        // If we're going to use the precomputed coinbase (empty block) and there are
        // transactions waiting in the mempool, make sure that on the next call to this
        // RPC, we consider the transaction count to have changed so we return a new
        // template (that includes these transactions) so they don't get stuck.
        if (next_cb_mtx && mempool.size() > 0)
            nTransactionsUpdatedLast = 0;

        CBlockIndex* pindexPrevNew = chainActive.Tip();
        nStart = GetTime();

        // Create new block
        if (pblocktemplate) {
            delete pblocktemplate;
            pblocktemplate = nullptr;
        }

        // Throw an error if no address valid for mining was provided.
        if (!std::visit(IsValidMinerAddress(), minerAddress)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "No miner address available (mining requires a wallet or -mineraddress)");
        }

        pblocktemplate = CreateNewBlock(Params(), minerAddress, next_cb_mtx);
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Mark script as important because it was used at least for one coinbase output
        std::visit(KeepMinerAddress(), minerAddress);

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience

    const Consensus::Params& consensus = Params().GetConsensus();

    // Update nTime
    UpdateTime(pblock, consensus, pindexPrev);
    pblock->nNonce = uint256();

    UniValue aCaps(UniValue::VARR);
    aCaps.push_back("proposal");

    UniValue txCoinbase = NullUniValue;
    UniValue transactions(UniValue::VARR);
    map<uint256, int64_t> setTxIndex;
    int i = 0;
    for (const CTransaction& tx : pblock->vtx) {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase() && !coinbasetxn)
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));

        entry.pushKV("hash", txHash.GetHex());

        UniValue deps(UniValue::VARR);
        for (const CTxIn& in : tx.vin) {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = i - 1;
        entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
        entry.pushKV("sigops", pblocktemplate->vTxSigOps[index_in_template]);

        if (tx.IsCoinBase()) {
            // Show founders' reward if it is required
            auto nextHeight = pindexPrev->nHeight + 1;
            bool canopyActive = consensus.NetworkUpgradeActive(nextHeight, Consensus::UPGRADE_CANOPY);
            if (!canopyActive && nextHeight > 0 && nextHeight <= consensus.GetLastFoundersRewardBlockHeight(nextHeight)) {
                CAmount nBlockSubsidy = GetBlockSubsidy(nextHeight, consensus);
                entry.pushKV("foundersreward", nBlockSubsidy / 5);
            }
            entry.pushKV("required", true);
            txCoinbase = entry;
        } else {
            transactions.push_back(entry);
        }
    }

    UniValue aux(UniValue::VOBJ);
    aux.pushKV("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end()));

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    static UniValue aMutable(UniValue::VARR);
    if (aMutable.empty()) {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("capabilities", aCaps);
    result.pushKV("version", pblock->nVersion);
    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("blockcommitmentshash", pblock->hashBlockCommitments.GetHex());
    // Deprecated; remove in a future release.
    result.pushKV("lightclientroothash", pblock->hashBlockCommitments.GetHex());
    // Deprecated; remove in a future release.
    result.pushKV("finalsaplingroothash", pblock->hashBlockCommitments.GetHex());
    result.pushKV("transactions", transactions);
    if (coinbasetxn) {
        assert(txCoinbase.isObject());
        result.pushKV("coinbasetxn", txCoinbase);
    } else {
        result.pushKV("coinbaseaux", aux);
        result.pushKV("coinbasevalue", (int64_t)pblock->vtx[0].vout[0].nValue);
    }
    result.pushKV("longpollid", chainActive.Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast));
    result.pushKV("target", hashTarget.GetHex());
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast() + 1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");
    result.pushKV("sigoplimit", (int64_t)MAX_BLOCK_SIGOPS);
    result.pushKV("sizelimit", (int64_t)MAX_BLOCK_SIZE);
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    result.pushKV("height", (int64_t)(pindexPrev->nHeight + 1));

    return result;
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256& hashIn) : hash(hashIn), found(false), state(){};

protected:
    virtual void BlockChecked(const CBlock& block, const CValidationState& stateIn)
    {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    };
};

UniValue submitblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("\"hexdata\" ( \"jsonparametersobject\" )")
                .set_description("Attempts to submit new block to network.\n"
                                 "The 'jsonparametersobject' parameter is currently ignored.\n"
                                 "See https://en.bitcoin.it/wiki/BIP_0022 for full specification."
                                 "\nFor more information on submitblock parameters and results, see: https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki#block-submission")
                .set_arguments("1. \"hexdata\"    (string, required) the hex-encoded block data to submit\n"
                               "2. \"jsonparametersobject\"     (string, optional) object of optional parameters\n"
                               "    {\n"
                               "      \"workid\" : \"id\"    (string, optional) if the server provided a workid, it MUST be included with submissions\n"
                               "    }")
                .set_result("\"duplicate\" - node already has valid copy of block\n"
                            "\"duplicate-invalid\" - node already has block, but it is invalid\n"
                            "\"duplicate-inconclusive\" - node already has block but has not validated it\n"
                            "\"inconclusive\" - node has not validated the block, it may not be on the node's current best chain\n"
                            "\"rejected\" - block was rejected as invalid")
                .set_examples("\"mydata\"");
        throw runtime_error(
            help_sections.combine_sections());
    }

    CBlock block;
    if (!DecodeHexBlk(block, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    uint256 hash = block.GetHash();
    bool fBlockPresent = false;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex* pindex = mi->second;
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                return "duplicate";
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return "duplicate-invalid";
            // Otherwise, we might only have the header - process the block before returning
            fBlockPresent = true;
        }
    }

    CValidationState state;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool fAccepted = ProcessNewBlock(state, Params(), NULL, &block, true, NULL);
    UnregisterValidationInterface(&sc);
    if (fBlockPresent) {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";
        return "duplicate";
    }
    if (fAccepted) {
        if (!sc.found)
            return "inconclusive";
        state = sc.state;
    }
    return BIP22ValidationResult(state);
}

UniValue estimatefee(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("nblocks")
                .set_description("Estimates the approximate fee per kilobyte\n"
                                 "needed for a transaction to begin confirmation\n"
                                 "within nblocks blocks.")
                .set_arguments("1. nblocks     (numeric)")
                .set_result("n :    (numeric) estimated fee-per-kilobyte\n"
                            "\n"
                            "-1.0 is returned if not enough transactions and\n"
                            "blocks have been observed to make an estimate.")
                .set_examples("6");

        throw runtime_error(help_sections.combine_sections());
    }
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    CFeeRate feeRate = mempool.estimateFee(nBlocks);
    if (feeRate == CFeeRate(0))
        return -1.0;

    return ValueFromAmount(feeRate.GetFeePerK());
}

UniValue estimatepriority(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("nblocks")
                .set_description("Estimates the approximate priority\n"
                                 "a zero-fee transaction needs to begin confirmation\n"
                                 "within nblocks blocks. \n"
                                 "-1.0 is returned if not enough transactions and\n"
                                 "blocks have been observed to make an estimate.")
                .set_arguments("1. nblocks     (numeric)")
                .set_result("n :    (numeric) estimated priority")
                .set_examples("6");
        throw runtime_error(help_sections.combine_sections());
    }

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    return mempool.estimatePriority(nBlocks);
}

UniValue getblocksubsidy(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1) {
        HelpSections help_sections =
            HelpSections(__func__)
                .set_usage("height")
                .set_description("Returns block subsidy reward, taking into account the mining slow start and the founders reward, of block at index provided.")
                .set_arguments("1. height         (numeric, optional) The block height.  If not provided, defaults to the current height of the chain.")
                .set_result("{\n"
                            "  \"miner\" : x.xxx,              (numeric) The mining reward amount in " +
                            CURRENCY_UNIT + ".\n"
                                            "  \"founders\" : x.xxx,           (numeric) The founders' reward amount in " +
                            CURRENCY_UNIT + ".\n"
                                            "  \"fundingstreams\" : [          (array) An array of funding stream descriptions (present only when Canopy has activated).\n"
                                            "    {\n"
                                            "      \"recipient\" : \"...\",        (string) A description of the funding stream recipient.\n"
                                            "      \"specification\" : \"url\",    (string) A URL for the specification of this funding stream.\n"
                                            "      \"value\" : x.xxx             (numeric) The funding stream amount in " +
                            CURRENCY_UNIT + ".\n"
                                            "      \"valueZat\" : xxxx           (numeric) The funding stream amount in " +
                            MINOR_CURRENCY_UNIT + ".\n"
                                                  "      \"address\" :                 (string) The transparent or Sapling address of the funding stream recipient.\n"
                                                  "    }, ...\n"
                                                  "  ]\n"
                                                  "}")
                .set_examples("1000");
        throw runtime_error(
            help_sections.combine_sections());
    }

    LOCK(cs_main);
    int nHeight = (params.size() == 1) ? params[0].get_int() : chainActive.Height();
    if (nHeight < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    const Consensus::Params& consensus = Params().GetConsensus();
    CAmount nBlockSubsidy = GetBlockSubsidy(nHeight, consensus);
    CAmount nMinerReward = nBlockSubsidy;
    CAmount nFoundersReward = 0;
    bool canopyActive = consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_CANOPY);

    UniValue result(UniValue::VOBJ);
    if (canopyActive) {
        KeyIO keyIO(Params());
        UniValue fundingstreams(UniValue::VARR);
        auto fsinfos = Consensus::GetActiveFundingStreams(nHeight, consensus);
        for (int idx = 0; idx < fsinfos.size(); idx++) {
            const auto& fsinfo = fsinfos[idx];
            CAmount nStreamAmount = fsinfo.Value(nBlockSubsidy);
            nMinerReward -= nStreamAmount;

            UniValue fsobj(UniValue::VOBJ);
            fsobj.pushKV("recipient", fsinfo.recipient);
            fsobj.pushKV("specification", fsinfo.specification);
            fsobj.pushKV("value", ValueFromAmount(nStreamAmount));
            fsobj.pushKV("valueZat", nStreamAmount);

            auto fs = consensus.vFundingStreams[idx];
            auto address = fs.value().RecipientAddress(consensus, nHeight);

            CScript* outpoint = std::get_if<CScript>(&address);
            std::string addressStr;

            if (outpoint != nullptr) {
                // For transparent funding stream addresses
                UniValue pubkey(UniValue::VOBJ);
                ScriptPubKeyToUniv(*outpoint, pubkey, true);
                addressStr = find_value(pubkey, "addresses").get_array()[0].get_str();

            } else {
                libzcash::SaplingPaymentAddress* zaddr = std::get_if<libzcash::SaplingPaymentAddress>(&address);
                if (zaddr != nullptr) {
                    // For shielded funding stream addresses
                    addressStr = keyIO.EncodePaymentAddress(*zaddr);
                }
            }

            fsobj.pushKV("address", addressStr);
            fundingstreams.push_back(fsobj);
        }
        result.pushKV("fundingstreams", fundingstreams);
    } else if (nHeight > 0 && nHeight <= consensus.GetLastFoundersRewardBlockHeight(nHeight)) {
        nFoundersReward = nBlockSubsidy / 5;
        nMinerReward -= nFoundersReward;
    }
    result.pushKV("miner", ValueFromAmount(nMinerReward));
    result.pushKV("founders", ValueFromAmount(nFoundersReward));
    return result;
}

static const CRPCCommand commands[] =
    {
        //  category              name                      actor (function)         okSafeMode
        //  --------------------- ------------------------  -----------------------  ----------
        {"mining", "getlocalsolps", &getlocalsolps, true},
        {"mining", "getnetworksolps", &getnetworksolps, true},
        {"mining", "getnetworkhashps", &getnetworkhashps, true},
        {"mining", "getmininginfo", &getmininginfo, true},
        {"mining", "prioritisetransaction", &prioritisetransaction, true},
        {"mining", "getblocktemplate", &getblocktemplate, true},
        {"mining", "submitblock", &submitblock, true},
        {"mining", "getblocksubsidy", &getblocksubsidy, true},

#ifdef ENABLE_MINING
        {"generating", "getgenerate", &getgenerate, true},
        {"generating", "setgenerate", &setgenerate, true},
        {"generating", "generate", &generate, true},
#endif

        {"util", "estimatefee", &estimatefee, true},
        {"util", "estimatepriority", &estimatepriority, true},
};

void RegisterMiningRPCCommands(CRPCTable& tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
