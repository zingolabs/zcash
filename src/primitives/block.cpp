// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "primitives/block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"

#include <algorithm>

const unsigned char ZCASH_AUTH_DATA_HASH_PERSONALIZATION[BLAKE2bPersonalBytes] =
    {'Z','c','a','s','h','A','u','t','h','D','a','t','H','a','s','h'};
const unsigned char ZCASH_BLOCK_COMMITMENTS_HASH_PERSONALIZATION[BLAKE2bPersonalBytes] =
    {'Z','c','a','s','h','B','l','o','c','k','C','o','m','m','i','t'};

uint256 DeriveBlockCommitmentsHash(
    uint256 hashChainHistoryRoot,
    uint256 hashAuthDataRoot)
{
    // https://zips.z.cash/zip-0244#block-header-changes
    CBLAKE2bWriter ss(SER_GETHASH, 0, ZCASH_BLOCK_COMMITMENTS_HASH_PERSONALIZATION);
    ss << hashChainHistoryRoot;
    ss << hashAuthDataRoot;
    ss << uint256(); // terminator
    return ss.GetHash();
}

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CBlock::BuildMerkleTree(bool* fMutated) const
{
    /* WARNING! If you're reading this because you're learning about crypto
       and/or designing a new system that will use merkle trees, keep in mind
       that the following merkle tree algorithm has a serious flaw related to
       duplicate txids, resulting in a vulnerability (CVE-2012-2459).

       The reason is that if the number of hashes in the list at a given time
       is odd, the last one is duplicated before computing the next level (which
       is unusual in Merkle trees). This results in certain sequences of
       transactions leading to the same merkle root. For example, these two
       trees:

                    A               A
                  /  \            /   \
                B     C         B       C
               / \    |        / \     / \
              D   E   F       D   E   F   F
             / \ / \ / \     / \ / \ / \ / \
             1 2 3 4 5 6     1 2 3 4 5 6 5 6

       for transaction lists [1,2,3,4,5,6] and [1,2,3,4,5,6,5,6] (where 5 and
       6 are repeated) result in the same root hash A (because the hash of both
       of (F) and (F,F) is C).

       The vulnerability results from being able to send a block with such a
       transaction list, with the same merkle root, and the same block hash as
       the original without duplication, resulting in failed validation. If the
       receiving node proceeds to mark that block as permanently invalid
       however, it will fail to accept further unmodified (and thus potentially
       valid) versions of the same block. We defend against this by detecting
       the case where we would hash two identical hashes at the end of the list
       together, and treating that identically to the block having an invalid
       merkle root. Assuming no double-SHA256 collisions, this will detect all
       known ways of changing the transactions without affecting the merkle
       root.
    */
    vMerkleTree.clear();
    vMerkleTree.reserve(vtx.size() * 2 + 16); // Safe upper bound for the number of total nodes.
    for (std::vector<CTransaction>::const_iterator it(vtx.begin()); it != vtx.end(); ++it)
        vMerkleTree.push_back(it->GetHash());
    int j = 0;
    bool mutated = false;
    for (int nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2)
    {
        for (int i = 0; i < nSize; i += 2)
        {
            int i2 = std::min(i+1, nSize-1);
            if (i2 == i + 1 && i2 + 1 == nSize && vMerkleTree[j+i] == vMerkleTree[j+i2]) {
                // Two identical hashes at the end of the list at a particular level.
                mutated = true;
            }
            vMerkleTree.push_back(Hash(BEGIN(vMerkleTree[j+i]),  END(vMerkleTree[j+i]),
                                       BEGIN(vMerkleTree[j+i2]), END(vMerkleTree[j+i2])));
        }
        j += nSize;
    }
    if (fMutated) {
        *fMutated = mutated;
    }
    return (vMerkleTree.empty() ? uint256() : vMerkleTree.back());
}

std::vector<uint256> CBlock::GetMerkleBranch(int nIndex) const
{
    if (vMerkleTree.empty())
        BuildMerkleTree();
    std::vector<uint256> vMerkleBranch;
    int j = 0;
    for (int nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2)
    {
        int i = std::min(nIndex^1, nSize-1);
        vMerkleBranch.push_back(vMerkleTree[j+i]);
        nIndex >>= 1;
        j += nSize;
    }
    return vMerkleBranch;
}

uint256 CBlock::CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex)
{
    if (nIndex == -1)
        return uint256();
    for (std::vector<uint256>::const_iterator it(vMerkleBranch.begin()); it != vMerkleBranch.end(); ++it)
    {
        if (nIndex & 1)
            hash = Hash(BEGIN(*it), END(*it), BEGIN(hash), END(hash));
        else
            hash = Hash(BEGIN(hash), END(hash), BEGIN(*it), END(*it));
        nIndex >>= 1;
    }
    return hash;
}

// Return 0 if x == 0, otherwise the smallest power of 2 greater than or equal to x.
// Algorithm based on <https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2>.
static uint64_t next_pow2(uint64_t x)
{
    x -= 1;
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    x |= (x >> 32);
    return x + 1;
}

uint256 CBlock::BuildAuthDataMerkleTree() const
{
    std::vector<uint256> tree;
    auto perfectSize = next_pow2(vtx.size());
    assert((perfectSize & (perfectSize - 1)) == 0);
    size_t expectedSize = std::max(perfectSize*2, (uint64_t)1) - 1;  // The total number of nodes.
    tree.reserve(expectedSize);

    // Add the leaves to the tree. v1-v4 transactions will append empty leaves.
    for (auto &tx : vtx) {
        tree.push_back(tx.GetAuthDigest());
    }
    // Append empty leaves until we get a perfect tree.
    tree.insert(tree.end(), perfectSize - vtx.size(), uint256());
    assert(tree.size() == perfectSize);

    int j = 0;
    for (int layerWidth = perfectSize; layerWidth > 1; layerWidth = layerWidth / 2) {
        for (int i = 0; i < layerWidth; i += 2) {
            CBLAKE2bWriter ss(SER_GETHASH, 0, ZCASH_AUTH_DATA_HASH_PERSONALIZATION);
            ss << tree[j + i];
            ss << tree[j + i + 1];
            tree.push_back(ss.GetHash());
        }

        // Move to the next layer.
        j += layerWidth;
    }

    assert(tree.size() == expectedSize);
    return (tree.empty() ? uint256() : tree.back());
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, hashBlockCommitments=%s, nTime=%u, nBits=%08x, nNonce=%s, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        hashBlockCommitments.ToString(),
        nTime, nBits, nNonce.ToString(),
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    s << "  vMerkleTree: ";
    for (unsigned int i = 0; i < vMerkleTree.size(); i++)
        s << " " << vMerkleTree[i].ToString();
    s << "\n";
    return s.str();
}
