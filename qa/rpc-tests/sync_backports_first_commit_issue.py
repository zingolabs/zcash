#!/usr/bin/env python3
# Copyright (c) 2018 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_true, start_nodes

class WalletImportExportTest (BitcoinTestFramework):
    def setup_network(self, split=False):
        num_nodes = 1
        extra_args = [["-exportdir={}/export{}".format(self.options.tmpdir, i)] for i in range(num_nodes)]
        self.nodes = start_nodes(num_nodes, self.options.tmpdir, extra_args)

    def run_test(self):
        sapling_address2 = self.nodes[0].z_getnewaddress('sapling')
        privkey2 = self.nodes[0].z_exportkey(sapling_address2)
        self.nodes[0].z_importkey(privkey2)

if __name__ == '__main__':
    WalletImportExportTest().main()
