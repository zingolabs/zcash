#ifndef RPC_DOCSTRINGS_H
#define RPC_DOCSTRINGS_H

using namespace std;

struct HelpSections {
    // begin data section
    string name;
    string usage;
    string description;
    string arguments;
    string result;
    string examples;
    
    HelpSections(string rpc_name): 
        // constructor: includes defaults
        name(rpc_name),
        usage(""),
        description(""),
        arguments(""),
        result("This RPC does not return a result by default.\n"),
        examples("") {}

    // begin method section
    string combine_sections() {
        // formats data section members into help message
        string argstring = "";
        if (!this->arguments.empty()) argstring += "\n\nArguments:\n" + this->arguments;
        return "Usage:\n" + this->name + " " + usage + "\n\nDescription:\n" + this->description + argstring + "\n\nResult:\n" + this->result + "\n\nExamples:\n" + this->examples;
    }

    // setter methods below.
    HelpSections& set_usage(string usage_message) {
        this->usage = usage_message;
        return *this;
    }
    HelpSections& set_description(string description_message) {
        this->description = description_message;
        return *this;
    }
    HelpSections& set_arguments(string arguments_message) {
        this->arguments = arguments_message;
        return *this;
    }
    HelpSections& set_result(string result_message) {
        this->result = result_message;
        return *this;
    }
    HelpSections& set_examples(string args) {
        this->examples +=
            this->make_example_cli(args) + this->make_example_rpc(args);
        return *this;
    }
    
    // helper methods below
    string make_example_cli(string args){
        return "> zcash-cli " + this->name + " " + args + "\n";
    }
    string make_example_rpc(string args){
        return "> curl --user myusername --data-binary '{\"jsonrpc\": \"1.0\", \"id\":\"curltest\", "
        "\"method\": \"" + this->name + "\", \"params\": [" + args + "] }' -H 'content-type: text/plain;' http://127.0.0.1:8232/\n";
    }

};

const std::string RAWTRANSACTION_DESCRIPTION = "{\n"
            "  \"in_active_chain\": b,   (boolean) Whether specified block is in the active chain or not (only present with explicit \"blockhash\" argument)\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"expiryheight\" : ttt,   (numeric, optional) The block height after which the transaction expires\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"zcashaddress\"          (string) Zcash address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vjoinsplit\" : [        (array of json objects, only for version >= 2)\n"
            "     {\n"
            "       \"vpub_old\" : x.xxx,         (numeric) public input value in " + CURRENCY_UNIT + "\n"
            "       \"vpub_new\" : x.xxx,         (numeric) public output value in " + CURRENCY_UNIT + "\n"
            "       \"anchor\" : \"hex\",         (string) the anchor\n"
            "       \"nullifiers\" : [            (json array of string)\n"
            "         \"hex\"                     (string) input note nullifier\n"
            "         ,...\n"
            "       ],\n"
            "       \"commitments\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note commitment\n"
            "         ,...\n"
            "       ],\n"
            "       \"onetimePubKey\" : \"hex\",  (string) the onetime public key used to encrypt the ciphertexts\n"
            "       \"randomSeed\" : \"hex\",     (string) the random seed\n"
            "       \"macs\" : [                  (json array of string)\n"
            "         \"hex\"                     (string) input note MAC\n"
            "         ,...\n"
            "       ],\n"
            "       \"proof\" : \"hex\",          (string) the zero-knowledge proof\n"
            "       \"ciphertexts\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note ciphertext\n"
            "         ,...\n"
            "       ]\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n";

const std::string GETRAWTRANSACTION_HELP =
            "getrawtransaction \"txid\" ( verbose \"blockhash\" )\n"
            "\nNOTE: If \"blockhash\" is not provided and the -txindex option is not enabled, then this call only\n"
            "works for mempool transactions. If either \"blockhash\" is provided or the -txindex option is\n"
            "enabled, it also works for blockchain transactions. If the block which contains the transaction\n"
            "is known, its hash can be provided even for nodes without -txindex. Note that if a blockhash is\n"
            "provided, only that block will be searched and if the transaction is in the mempool or other\n"
            "blocks, or if this node does not have the given block available, the transaction will not be found.\n"
            "\nReturn the raw transaction data.\n"
            "\nIf verbose=0, returns a string that is serialized, hex-encoded data for 'txid'.\n"
            "If verbose is non-zero, returns an Object with information about 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose     (numeric, optional, default=0) If 0, return a string of hex-encoded data, otherwise return a JSON object\n"
            "3. \"blockhash\" (string, optional) The block in which to look for the transaction\n"

            "\nResult (if verbose is not set or set to 0):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose > 0):\n"
            + RAWTRANSACTION_DESCRIPTION +
            "\nExamples:\n";

#endif
