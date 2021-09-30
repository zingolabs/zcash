// Copyright (c) 2019 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "fs.h"
#include "main.h"
#include "test/test_bitcoin.h"
#include "test/test_random.h"
#include <span.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(streams_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(streams_buffered_file)
{
    unsigned char a(1);
    unsigned char b(2);
    unsigned char bytes[] = { 3, 4, 5, 6 };
    std::vector<unsigned char> vch;

    // Each test runs twice. Serializing a second time at the same starting
    // point should yield the same results, even if the first test grew the
    // vector.

    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 0, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{1, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 0, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{1, 2}}));
    vch.clear();

    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 1, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 1, 2}}));
    vch.clear();

    vch.resize(5, 0);
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 1, 2, 0}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 1, 2, 0}}));
    vch.clear();

    vch.resize(4, 0);
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 3, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 0, 1, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 3, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 0, 1, 2}}));
    vch.clear();

    vch.resize(4, 0);
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 4, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 0, 0, 1, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 4, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 0, 0, 1, 2}}));
    vch.clear();

    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 0, bytes);
    BOOST_CHECK((vch == std::vector<unsigned char>{{3, 4, 5, 6}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 0, bytes);
    BOOST_CHECK((vch == std::vector<unsigned char>{{3, 4, 5, 6}}));
    vch.clear();

    vch.resize(4, 8);
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, bytes, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{8, 8, 1, 3, 4, 5, 6, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, bytes, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{8, 8, 1, 3, 4, 5, 6, 2}}));
    vch.clear();
}

BOOST_AUTO_TEST_CASE(streams_buffered_file_rand)
{
    // Make this test deterministic.
    seed_insecure_rand(true);

    for (int rep = 0; rep < 500; ++rep) {
        FILE* file = fopen("streams_test_tmp", "w+b");
        size_t fileSize = GetRandInt(256);
        for (uint8_t i = 0; i < fileSize; ++i) {
            fwrite(&i, 1, 1, file);
        }
        rewind(file);

        size_t bufSize = GetRandInt(300) + 1;
        size_t rewindSize = GetRandInt(bufSize);
        CBufferedFile bf(file, bufSize, rewindSize, 222, 333);
        size_t currentPos = 0;
        size_t maxPos = 0;
        for (int step = 0; step < 100; ++step) {
            if (currentPos >= fileSize)
                break;

            // We haven't read to the end of the file yet.
            BOOST_CHECK(!bf.eof());
            BOOST_CHECK_EQUAL(bf.GetPos(), currentPos);

            // Pretend the file consists of a series of objects of varying
            // sizes; the boundaries of the objects can interact arbitrarily
            // with the CBufferFile's internal buffer. These first three
            // cases simulate objects of various sizes (1, 2, 5 bytes).
            switch (GetRandInt(5)) {
            case 0: {
                uint8_t a[1];
                if (currentPos + 1 > fileSize)
                    continue;
                bf.SetLimit(currentPos + 1);
                bf >> MakeSpan(a);
                for (uint8_t i = 0; i < 1; ++i) {
                    BOOST_CHECK_EQUAL(a[i], currentPos);
                    currentPos++;
                }
                break;
            }
            case 1: {
                uint8_t a[2];
                if (currentPos + 2 > fileSize)
                    continue;
                bf.SetLimit(currentPos + 2);
                bf >> MakeSpan(a);
                for (uint8_t i = 0; i < 2; ++i) {
                    BOOST_CHECK_EQUAL(a[i], currentPos);
                    currentPos++;
                }
                break;
            }
            case 2: {
                uint8_t a[5];
                if (currentPos + 5 > fileSize)
                    continue;
                bf.SetLimit(currentPos + 5);
                bf >> MakeSpan(a);
                for (uint8_t i = 0; i < 5; ++i) {
                    BOOST_CHECK_EQUAL(a[i], currentPos);
                    currentPos++;
                }
                break;
            }
            case 3: {
                // Find a byte value (that is at or ahead of the current position).
                size_t find = currentPos + GetRandInt(8);
                if (find >= fileSize)
                    find = fileSize - 1;
                bf.FindByte(static_cast<char>(find));
                // The value at each offset is the offset.
                BOOST_CHECK_EQUAL(bf.GetPos(), find);
                currentPos = find;

                bf.SetLimit(currentPos + 1);
                uint8_t i;
                bf >> i;
                BOOST_CHECK_EQUAL(i, currentPos);
                currentPos++;
                break;
            }
            case 4: {
                size_t requestPos = GetRandInt(maxPos + 4);
                bool okay = bf.SetPos(requestPos);
                // The new position may differ from the requested position
                // because we may not be able to rewind beyond the rewind
                // window, and we may not be able to move forward beyond the
                // farthest position we've reached so far.
                currentPos = bf.GetPos();
                BOOST_CHECK_EQUAL(okay, currentPos == requestPos);
                // Check that we can position within the rewind window.
                if (requestPos <= maxPos &&
                    maxPos > rewindSize &&
                    requestPos >= maxPos - rewindSize) {
                    // We requested a position within the rewind window.
                    BOOST_CHECK(okay);
                }
                break;
            }
            }
            if (maxPos < currentPos)
                maxPos = currentPos;
        }
    }
    fs::remove("streams_test_tmp");
}

BOOST_AUTO_TEST_SUITE_END()
