/*  VTC Blockindexer - A utility to build additional indexes to the 
    Vertcoin blockchain by scanning and indexing the blockfiles
    downloaded by Vertcoin Core.
    
    Copyright (C) 2017  Gert-Jaap Glasbergen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "httpserver.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <vector>
#include <memory>
#include <cstdlib>
#include <restbed>
#include "json.hpp"
#include "utility.h"
using namespace std;
using namespace restbed;
using json = nlohmann::json;


VtcBlockIndexer::HttpServer::HttpServer(shared_ptr<leveldb::DB> db, shared_ptr<VtcBlockIndexer::MempoolMonitor> mempoolMonitor, string blocksDir) {
    this->db = db;
    this->blocksDir = blocksDir;
    this->mempoolMonitor = mempoolMonitor;
    blockReader.reset(new VtcBlockIndexer::BlockReader(blocksDir));
    scriptSolver = std::make_unique<VtcBlockIndexer::ScriptSolver>();
    httpClient.reset(new jsonrpc::HttpClient("http://" + std::string(std::getenv("COIND_RPCUSER")) + ":" + std::string(std::getenv("COIND_RPCPASSWORD")) + "@" + std::string(std::getenv("COIND_HOST")) + ":" + std::string(std::getenv("COIND_RPCPORT"))));
    vertcoind.reset(new VertcoinClient(*httpClient));
}

void VtcBlockIndexer::HttpServer::mempoolTransactionIds(const shared_ptr<Session> session) {
    const auto request = session->get_request();
    
    vector<std::string> txIds = mempoolMonitor->getTxIds();
    json j = json::array();
    for (string txid : txIds) {
        j.push_back(txid);
    }
    string body = j.dump();
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
}


void VtcBlockIndexer::HttpServer::getTransaction(const shared_ptr<Session> session) {
    const auto request = session->get_request();
    
    cout << "Looking up txid " << request->get_path_parameter("id") << endl;
    
    try {
        const Json::Value tx = vertcoind->getrawtransaction(request->get_path_parameter("id"), true);
        
        stringstream body;
        body << tx.toStyledString();
        
        session->close(OK, body.str(), {{"Content-Type","application/json"},{"Content-Length",  std::to_string(body.str().size())}});
    } catch(const jsonrpc::JsonRpcException& e) {
        const std::string message(e.what());
        cout << "Not found " << message << endl;
        session->close(404, message, {{"Content-Type","application/json"},{"Content-Length",  std::to_string(message.size())}});
    }
}

void VtcBlockIndexer::HttpServer::getBlock(const shared_ptr<Session> session) {
    const auto request = session->get_request();
    
    string highestBlockString;
    this->db->Get(leveldb::ReadOptions(),"highestblock",&highestBlockString);

    uint64_t highestBlock = stoll(highestBlockString);

    std::string blockHashString = request->get_path_parameter("hash","");

    string blockHeightString;
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(),"block-hash-" + blockHashString,&blockHeightString);
    if(!s.ok()) // no key found
    { 
        const std::string message("Block not found");
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
        return;
    }



    uint64_t blockHeight = stoll(blockHeightString);

    stringstream blockKey;
    blockKey << "block-filePosition-" << setw(8) << setfill('0') << blockHeight;


    std::string filePosition;
    s = this->db->Get(leveldb::ReadOptions(), blockKey.str(), &filePosition);
    if(!s.ok()) // no key found
    {
        const std::string message("Block not found");
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
        return;
    }
    
    Block block = this->blockReader->readBlock(filePosition.substr(0,12),stoll(filePosition.substr(12,12)),blockHeight,false);

    json jsonBlock;
    jsonBlock["hash"] = block.blockHash;
    jsonBlock["previousBlockHash"] = block.previousBlockHash;
    jsonBlock["merkleRoot"] = block.merkleRoot;
    jsonBlock["version"] = block.version;
    jsonBlock["time"] = block.time;
    jsonBlock["bits"] = block.bits;
    jsonBlock["nonce"] = block.nonce;
    jsonBlock["height"] = block.height;
    jsonBlock["confirmations"] = highestBlock-block.height+1;
    jsonBlock["size"] = block.byteSize;

    json txs = json::array();


    for (VtcBlockIndexer::Transaction tx : block.transactions) {
        txs.push_back(tx.txHash);
    }

    jsonBlock["tx"] = txs;
    jsonBlock["ismainchain"] = true;

    string body = jsonBlock.dump();
    
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
}
/*
package models

type ScriptSig struct {
	Hex 				string					`json:"hex"`
	Asm  				string 					`json:"asm"`
}

type TransactionVin struct {	
	CoinBase 			string 						`json:"coinbase"`
	Sequence  			int64						`json:"sequence"`
	Number				int 						`json:"n"`
	TxId 				string 						`json:"txid,omitempty"`
	Vout 				int 						`json:"vout,omitempty"`
	ScriptSig 			ScriptSig					`json:"scriptSig,omitempty"`
	Address 			string 						`json:"addr,omitempty"`
	ValueSatoshi		int64						`json:"valueSat,omitempty"`
	Value 				float64 					`json:"value,omitempty"`
	DoubleSpentTxID 	string 						`json:"doubleSpentTxID,omitempty"`
}

type ScriptPubKey struct {
	Hex 				string					`json:"hex"`
	Asm  				string 					`json:"asm"`
	Addresses			[]string 				`json:"addresses"`
	Type 				string 					`json:"type"`
}

type TransactionVout struct {
	Value 				float64							`json:"value"`
	Number 				int 							`json:"n"`
	ScriptPubKey 		ScriptPubKey					`json:"scriptPubKey"`
	SpentTxId 			string							`json:"spentTxId,omitempty"`
	SpentIndex 			int 							`json:"spentIndex,omitempty"`
	SpentHeight 		int  							`json:"spentHeight,omitempty"`
}

type Transaction struct {
	TxId 				string					`json:"txid"`
	Version				int 					`json:"version"`
	LockTime 			int 					`json:"locktime"`
	Vin 				[]TransactionVin 		`json:"vin"`
	Vout 				[]TransactionVout	 	`json:"vout"`
	BlockHash 			string 					`json:"blockhash"`
	BlockHeight 		int 					`json:"blockheight"`
	Confirmations 		int 					`json:"confirmations"`
	Time 				int64 					`json:"time"`
	BlockTime 			int64 					`json:"blocktime"`
	IsCoinBase 			bool 					`json:"isCoinBase"`
	ValueOut 			float64 				`json:"valueOut"`
	Size 				int 					`json:"size"`
	ValueIn 			float64 				`json:"valueIn"`
	Fees	 			float64 				`json:"fees"`	
}

type TxsResponse struct {
	PagesTotal			int 					`json:"pagesTotal"`
	Txs					[]Transaction			`json:"txs"`
}*/

vector<string> VtcBlockIndexer::HttpServer::getAddressesForTxo(string txHash, uint64_t idx) {
    vector<string> returnValue = {};
    stringstream txoAddressKey;
    txoAddressKey << txHash << setw(8) << setfill('0') << idx << "-address-";
    string start(txoAddressKey.str() + "00000000");
    string limit(txoAddressKey.str() + "99999999");
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    for (it->Seek(start);
            it->Valid() && it->key().ToString() < limit;
            it->Next()) {
        returnValue.push_back(it->value().ToString());
    }

    return returnValue;
}

uint64_t VtcBlockIndexer::HttpServer::getValueForTxo(string txHash, uint64_t idx) {
    stringstream txoValueKey;
    txoValueKey << txHash << setw(8) << setfill('0') << idx << "-value";
    string valueString;
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), txoValueKey.str(), &valueString);
    if(!s.ok()) // no key found
    { 
        return 0;
    }
    return stoll(valueString);
}

void VtcBlockIndexer::HttpServer::getBlockTransactions(const shared_ptr<Session> session) {
    const auto request = session->get_request();
    
    string highestBlockString;
    this->db->Get(leveldb::ReadOptions(),"highestblock",&highestBlockString);

    uint64_t highestBlock = stoll(highestBlockString);

    std::string blockHashString = request->get_path_parameter("hash","");
    int pageNum = stoi(request->get_path_parameter("page","0"));

    string blockHeightString;
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(),"block-hash-" + blockHashString,&blockHeightString);
    if(!s.ok()) // no key found
    { 
        const std::string message("Block not found");
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
        return;
    }

    uint64_t blockHeight = stoll(blockHeightString);

    stringstream blockKey;
    blockKey << "block-filePosition-" << setw(8) << setfill('0') << blockHeight;

    std::string filePosition;
    s = this->db->Get(leveldb::ReadOptions(), blockKey.str(), &filePosition);
    if(!s.ok()) // no key found
    {
        const std::string message("Block not found");
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
        return;
    }
    
    Block block = this->blockReader->readBlock(filePosition.substr(0,12),stoll(filePosition.substr(12,12)),blockHeight,false);

    json response;
    size_t leftOver = block.transactions.size() % 10;

    response["pagesTotal"] = (block.transactions.size() - leftOver) / 10 + (leftOver > 0 ? 1 : 0);
    json txs = json::array();

    int pageStart = 10 * pageNum;
    int maxIndex = block.transactions.size()-1;
    int pageEnd = std::min(pageStart+10, maxIndex);

    if(pageEnd >= pageStart) {
        for (int i = pageStart; i <= pageEnd; i++) {
            VtcBlockIndexer::Transaction tx = block.transactions.at(i);
            json jtx;
            jtx["txid"] = tx.txHash;
            jtx["version"] = tx.version;
            jtx["locktime"] = tx.lockTime;
            jtx["size"] = tx.byteSize;
            jtx["confirmations"] = highestBlock-block.height+1;
            jtx["blockhash"] = block.blockHash;
            jtx["blockheight"] = block.height;
            jtx["isCoinBase"] = false;
            json vins = json::array();
            for (VtcBlockIndexer::TransactionInput txi : tx.inputs) {
                if(txi.coinbase) jtx["isCoinBase"] = true;

                json vin;
                vin["sequence"] = txi.sequence;
                vin["n"] = txi.index;
                vin["txid"] = txi.txHash;
                vin["vout"] = txi.txoIndex;
                json scriptSig;
                scriptSig["hex"] = Utility::hashToHex(txi.script);
                vin["scriptSig"] = scriptSig;
                vector<string> addresses = getAddressesForTxo(txi.txHash, txi.txoIndex);
                string addressesConcatenated = "";
                
                for(size_t i = 0; i < addresses.size(); i++) {
                    addressesConcatenated += (i > 0 ? " " : "") + addresses[i];
                }
                vin["addr"] = addressesConcatenated;
                vin["valueSat"] = getValueForTxo(txi.txHash, txi.txoIndex);
                
                vins.push_back(vin);
            }
            jtx["vin"] = vins;
            json vouts = json::array();
            for (VtcBlockIndexer::TransactionOutput txo : tx.outputs) {
                json vout;
                
                string spentTx;
                stringstream txoKey;
                txoKey << "txo-" << tx.txHash << "-" << setw(8) << setfill('0') << txo.index << "-spent";

                leveldb::Status s = this->db->Get(leveldb::ReadOptions(), txoKey.str(), &spentTx);
                if(s.ok()) // no key found, not spent. Add balance.
                {
                    vout["spentTxId"] = spentTx.substr(64, 64);
                    vout["spentIndex"] = stoll(spentTx.substr(128, 8));
                    vout["spentBlock"] = spentTx.substr(0, 64);
                    std::string blockHeightString;
                    s = this->db->Get(leveldb::ReadOptions(), "block-hash-" + spentTx.substr(0, 64), &blockHeightString);
                    if(s.ok()) 
                    {
                        vout["spentHeight"] = stoll(blockHeightString);
                    }
                }

                json scriptPubKey;
                scriptPubKey["hex"] = Utility::hashToHex(txo.script);
                scriptPubKey["addresses"] = json::array();
                vector<string> addresses = getAddressesForTxo(tx.txHash, txo.index);
                for(string address : addresses) {
                    scriptPubKey["addresses"].push_back(address);
                }
                scriptPubKey["type"] = scriptSolver->getScriptTypeName(txo.script);
                vout["scriptPubKey"] = scriptPubKey;
                vout["valueSat"] = txo.value;
                
                vouts.push_back(vout);
            }
            jtx["vout"] = vouts;
            txs.push_back(jtx);
        }
    }
    response["txs"] = txs;
    string body = response.dump();
    
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
}


void VtcBlockIndexer::HttpServer::getTransactionProof(const shared_ptr<Session> session) {
    const auto request = session->get_request();
    
    std::string blockHash;
    std::string txId = request->get_path_parameter("id","");
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), "tx-" + txId + "-block", &blockHash);
    if(!s.ok()) // no key found
    {
        const std::string message("TX not found");
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
        return;
    }

    std::string blockHeightString;
    s = this->db->Get(leveldb::ReadOptions(), "block-hash-" + blockHash, &blockHeightString);
    if(!s.ok()) // no key found
    {
        const std::string message("Block not found");
        session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
        return;
    }
    uint64_t blockHeight = stoll(blockHeightString);
    json j;
    j["txHash"] = txId;
    j["blockHash"] = blockHash;
    j["blockHeight"] = blockHeight;
    json chain = json::array();
    for(uint64_t i = blockHeight+1; --i > 0 && i > blockHeight-10;) {
        stringstream blockKey;
        blockKey << "block-filePosition-" << setw(8) << setfill('0') << i;
   
        std::string filePosition;
        s = this->db->Get(leveldb::ReadOptions(), blockKey.str(), &filePosition);
        if(!s.ok()) // no key found
        {
            const std::string message("Block not found");
            session->close(404, message, {{"Content-Length",  std::to_string(message.size())}});
            return;
        }
       
        Block block = this->blockReader->readBlock(filePosition.substr(0,12),stoll(filePosition.substr(12,12)),i,true);

        json jsonBlock;
        jsonBlock["blockHash"] = block.blockHash;
        jsonBlock["previousBlockHash"] = block.previousBlockHash;
        jsonBlock["merkleRoot"] = block.merkleRoot;
        jsonBlock["version"] = block.version;
        jsonBlock["time"] = block.time;
        jsonBlock["bits"] = block.bits;
        jsonBlock["nonce"] = block.nonce;
        jsonBlock["height"] = block.height;
        chain.push_back(jsonBlock);

    }
    j["chain"] = chain;
    string body = j.dump();
    
   session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
}

void VtcBlockIndexer::HttpServer::sync(const shared_ptr<Session> session) {
    json j;

    const auto request = session->get_request( );

    string highestBlockString;
    this->db->Get(leveldb::ReadOptions(),"highestblock",&highestBlockString);

    j["error"] = nullptr;
    j["height"] = stoll(highestBlockString);
    try {
        const Json::Value blockCount = vertcoind->getblockcount();
        
        j["blockChainHeight"] = blockCount.asInt();
    } catch(const jsonrpc::JsonRpcException& e) {
        const std::string message(e.what());
        j["error"] = message;
    }

    float progress = (float)j["height"].get<int>() / (float)j["blockChainHeight"].get<int>();
    progress *= 100;
    j["syncPercentage"] = progress;
    if(progress >= 100) {
        j["status"] = "finished";
    } else {
        j["status"] = "indexing";
    }

    string body = j.dump();
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
}

void VtcBlockIndexer::HttpServer::getBlocks(const shared_ptr<Session> session) {
    json j = json::array();

    const auto request = session->get_request( );

    string highestBlockString;
    this->db->Get(leveldb::ReadOptions(),"highestblock",&highestBlockString);
    
   
    long long limitParam = stoi(request->get_query_parameter("limit","0"));
    if(limitParam == 0 || limitParam > 100)
        limitParam = 100;

    long long lowestBlock = stoll(highestBlockString)-limitParam;
    stringstream lowestBlockString;
    lowestBlockString << setw(8) << setfill('0') << lowestBlock;

    string start("block-" + highestBlockString);
    string limit("block-" + lowestBlockString.str());
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    for (it->Seek(start);
            it->Valid() && it->key().ToString() > limit;
            it->Prev()) {
        json blockObj;
        string blockHeightString = it->key().ToString().substr(6);
        blockObj["hash"] = it->value().ToString();
        string blockSizeString;
        string blockTxesString;
        string blockTimeString;
        this->db->Get(leveldb::ReadOptions(),"block-size-" + blockHeightString,&blockSizeString);
        this->db->Get(leveldb::ReadOptions(),"block-txcount-" + blockHeightString,&blockTxesString);
        this->db->Get(leveldb::ReadOptions(),"block-time-" + blockHeightString,&blockTimeString);
        blockObj["height"] = stoll(blockHeightString);
        blockObj["size"] = stoll(blockSizeString);
        blockObj["time"] = stoll(blockTimeString);
        blockObj["txlength"] = stoll(blockTxesString);
        blockObj["poolInfo"] = nullptr;
        j.push_back(blockObj);
    }

    string body = j.dump();
    
   session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );

}

void VtcBlockIndexer::HttpServer::getBlocksByDate(const shared_ptr<Session> session) {
    json j = json::array();
 
    const auto request = session->get_request( );

    long long startParam = stoll(request->get_query_parameter("start","0"));
    long long endParam = stoll(request->get_query_parameter("end","0"));
    

    stringstream ssBlockHeightTimeStartKey;
    stringstream ssBlockHeightTimeEndKey;
    ssBlockHeightTimeStartKey << "block-hash-time-" << setw(12) << setfill('0') << startParam;
    ssBlockHeightTimeEndKey << "block-hash-time-" << setw(12) << setfill('0') << endParam;
    
    string start(ssBlockHeightTimeStartKey.str());
    string limit(ssBlockHeightTimeEndKey.str());
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    for (it->Seek(start);
            it->Valid() && it->key().ToString() <= limit;
            it->Next()) {
        json blockObj;
        string blockHashString = it->value().ToString();
        string blockHeightString;
        this->db->Get(leveldb::ReadOptions(),"block-hash-" + blockHashString,&blockHeightString);
        string blockSizeString;
        string blockTxesString;
        string blockTimeString;
        this->db->Get(leveldb::ReadOptions(),"block-size-" + blockHeightString,&blockSizeString);
        this->db->Get(leveldb::ReadOptions(),"block-txcount-" + blockHeightString,&blockTxesString);
        this->db->Get(leveldb::ReadOptions(),"block-time-" + blockHeightString,&blockTimeString);
        blockObj["hash"] = it->value().ToString();
        blockObj["height"] = stoll(blockHeightString);
        blockObj["size"] = stoll(blockSizeString);
        blockObj["time"] = stoll(blockTimeString);
        blockObj["txlength"] = stoll(blockTxesString);
        blockObj["poolInfo"] = nullptr;
        j.push_back(blockObj);
    }

    string body = j.dump();
     
   session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );

}

void VtcBlockIndexer::HttpServer::addressBalance( const shared_ptr< Session > session )
{
    long long balance = 0;
    long long unconfirmedBalance = 0;
    long long txCount = 0;
    long long unconfirmedTxCount = 0;
    int txoCount = 0;
    const auto request = session->get_request( );
    int details = stoi(request->get_query_parameter("details","0"));
    
    cout << "Checking balance for address " << request->get_path_parameter( "address" ) << endl;

    string start(request->get_path_parameter( "address" ) + "-txo-00000001");
    string limit(request->get_path_parameter( "address" ) + "-txo-99999999");
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    
    for (it->Seek(start);
            it->Valid() && it->key().ToString() < limit;
            it->Next()) {

        string spentTx;
        txoCount++;
        txCount++;
        string txo = it->value().ToString();

        leveldb::Status s = this->db->Get(leveldb::ReadOptions(), "txo-" + txo.substr(0,64) + "-" + txo.substr(64,8) + "-spent", &spentTx);
        if(!s.ok()) // no key found, not spent. Add balance.
        {
            balance += stoll(txo.substr(80));
            // check mempool for spenders
            string spender = mempoolMonitor->outpointSpend(txo.substr(0,64), stol(txo.substr(64,8)));
            if(spender.compare("") == 0) {
                unconfirmedBalance += stoll(txo.substr(80));
            } else {
                unconfirmedTxCount++;
            }
        } else { 
            txCount++;
        }
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;

    cout << "Analyzed " << txoCount << " TXOs - Balance is " << balance << endl;
 
    // Add mempool transactions
    vector<VtcBlockIndexer::TransactionOutput> mempoolOutputs = mempoolMonitor->getTxos(request->get_path_parameter( "address" ));
    for (VtcBlockIndexer::TransactionOutput txo : mempoolOutputs) {
        txoCount++;
        unconfirmedTxCount++;
        string spender = mempoolMonitor->outpointSpend(txo.txHash, txo.index);
        cout << "Spender for " << txo.txHash << "/" << txo.index << " = " << spender;
        if(spender.compare("") == 0) {
            unconfirmedBalance += txo.value;
        } else {
            unconfirmedTxCount++;
        }
    }

    cout << "Including mempool: Analyzed " << txoCount << " TXOs - Balance is " << balance << endl;
    
    if(details != 0) {
        json j;
        j["balance"] = balance;
        j["txCount"] = txCount;
        j["unconfirmedBalance"] = unconfirmedBalance;
        j["unconfirmedTxCount"] = unconfirmedTxCount;
        string body = j.dump();
        session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
    } else {
        stringstream body;
        body << balance;
        
        session->close( OK, body.str(), { {"Content-Type","text/plain"}, { "Content-Length",  std::to_string(body.str().size()) } } );
    }
    
}

void VtcBlockIndexer::HttpServer::addressTxos( const shared_ptr< Session > session )
{
    json j = json::array();

    const auto request = session->get_request( );

    long long sinceBlock = stoll(request->get_path_parameter( "sinceBlock", "0" ));
    
    int txHashOnly = stoi(request->get_query_parameter("txHashOnly","0"));
    int raw = stoi(request->get_query_parameter("raw","0"));
    int unspent = stoi(request->get_query_parameter("unspent","0"));
    int unconfirmed = stoi(request->get_query_parameter("unconfirmed","0"));
    int scripts = stoi(request->get_query_parameter("script","0"));
    cout << "Fetching address txos for address " << request->get_path_parameter( "address" ) << endl;
   
    string start(request->get_path_parameter( "address" ) + "-txo-00000001");
    string limit(request->get_path_parameter( "address" ) + "-txo-99999999");
    
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    
    for (it->Seek(start);
            it->Valid() && it->key().ToString() < limit;
            it->Next()) {

        string spentTx;
        string txo = it->value().ToString();

        leveldb::Status s = this->db->Get(leveldb::ReadOptions(), "txo-" + txo.substr(0,64) + "-" + txo.substr(64,8) + "-spent", &spentTx);
        long long block = stoll(txo.substr(72,8));

        stringstream ssBlockTimeHeightKey;
        string blockTimeStr;
        ssBlockTimeHeightKey << "block-time-" << setw(8) << setfill('0') << block;
        this->db->Get(leveldb::ReadOptions(), ssBlockTimeHeightKey.str(), &blockTimeStr);

        const long long blockTime = stoll(blockTimeStr);

        // If the block count param is greater than 2000/1/1 consider
        // it as a timestamp rather than block height
        const long long blockTimeCrossover = 946702800;

        if((block >= sinceBlock && sinceBlock < blockTimeCrossover) || 
           (blockTime >= sinceBlock && sinceBlock >= blockTimeCrossover)) {
            json txoObj;
            txoObj["height"] = block;
            txoObj["time"] = blockTime;

            if(!s.ok()) {
                if(unconfirmed) {
                    string spender = mempoolMonitor->outpointSpend(txo.substr(0,64), stol(txo.substr(64,8)));
                    if(spender.compare("") == 0) {
                        txoObj["spender"] = nullptr;
                    } else {
                        if(unspent == 1) continue;
                        txoObj["spender"] = spender;
                    }
                } else { 
                    txoObj["spender"] = nullptr;
                }
            } else {
                if(unspent == 1) continue;
                txoObj["spender"] = spentTx.substr(64, 64);

            }

            if(raw != 0) {
                try {
                    const Json::Value tx = vertcoind->getrawtransaction(txo.substr(0,64), false);
                    txoObj["tx"] = tx.asString();
                } catch(const jsonrpc::JsonRpcException& e) {
                    const std::string message(e.what());
                    session->close(400, message, {{"Content-Type","text/plain"},{"Content-Length",  std::to_string(message.size())}});
                    cout << "Not found " << message << endl;
                    return;
                }
            }

            if(raw == 0 && scripts != 0) {
                 try {
                    const Json::Value tx = vertcoind->getrawtransaction(txo.substr(0,64), true);
                    const Json::Value scriptHex = tx["vout"][stoi(txo.substr(64,8))]["scriptPubKey"]["hex"];
                    txoObj["script"] = scriptHex.asString();
                } catch(const jsonrpc::JsonRpcException& e) {
                    const std::string message(e.what());
                    session->close(400, message, {{"Content-Type","text/plain"},{"Content-Length",  std::to_string(message.size())}});
                    cout << "Not found " << message << endl;
                    return;
                }
            }

           

            if(raw != 0 && txoObj["spender"].is_string()) {
                try {
                    const Json::Value tx = vertcoind->getrawtransaction(txoObj["spender"].get<string>(), false);
                    txoObj["spender"] = tx.asString();
                } catch(const jsonrpc::JsonRpcException& e) {
                    const std::string message(e.what());
                    session->close(400, message, {{"Content-Type","text/plain"},{"Content-Length",  std::to_string(message.size())}});
                    cout << "Not found " << message << endl;
                    return;
                }
            }

            if(raw == 0) {
                txoObj["txhash"] = txo.substr(0,64);
            }
            if(txHashOnly == 0 && raw == 0) {
                txoObj["vout"] = stoll(txo.substr(64,8));
                txoObj["value"] = stoll(txo.substr(80));
            }

            j.push_back(txoObj);
        }
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;

    if(unconfirmed == 1) {
        // Add mempool transactions
        vector<VtcBlockIndexer::TransactionOutput> mempoolOutputs = mempoolMonitor->getTxos(request->get_path_parameter( "address" ));
        for (VtcBlockIndexer::TransactionOutput txo : mempoolOutputs) {
            json txoObj;
            txoObj["txhash"] = txo.txHash;
            txoObj["vout"] = txo.index;
            txoObj["value"] = txo.value;
            txoObj["block"] = 0;
            string spender = mempoolMonitor->outpointSpend(txo.txHash, txo.index);
            if(spender.compare("") != 0) {
                txoObj["spender"] = spender;
            } else {
                txoObj["spender"] = nullptr;
            }
            j.push_back(txoObj);
        }
    }

    string body = j.dump();
     
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
}

void VtcBlockIndexer::HttpServer::outpointSpend( const shared_ptr< Session > session )
{
    json j;
    j["error"] = false;
    const auto request = session->get_request( );
    int raw = stoi(request->get_query_parameter("raw","0"));
    int unconfirmed = stoi(request->get_query_parameter("unconfirmed","0"));
    
    long long vout = stoll(request->get_path_parameter( "vout", "0" ));
    string txid = request->get_path_parameter("txid", "");
    stringstream txBlockKey;
    string txBlock;
    txBlockKey << "tx-" << txid << "-block";
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), txBlockKey.str(), &txBlock);
    if(!s.ok()) {
        j["error"] = true;
        j["errorDescription"] = "Transaction ID not found";
    }
    else 
    {
        stringstream txoId;
        txoId << "txo-" << txid << "-" << setw(8) << setfill('0') << vout << "-spent";
        string spentTx;

        s = this->db->Get(leveldb::ReadOptions(), txoId.str(), &spentTx);
        j["spent"] = s.ok();
        if(s.ok()) {
            j["spender"] = spentTx.substr(64, 64);
            
            string blockHeightStr;
            stringstream blockHashId;
            blockHashId << "block-hash-" << spentTx.substr(0,64);
            s = this->db->Get(leveldb::ReadOptions(), blockHashId.str(), &blockHeightStr);
            if(s.ok()) {
                j["height"] = stol(blockHeightStr);
            }
        } else if(unconfirmed != 0) {
            string mempoolSpend = mempoolMonitor->outpointSpend(txid, vout);
            if(mempoolSpend.compare("") != 0) {
                j["spent"] = true;
                j["spender"] = mempoolSpend;
                j["height"] = 0;
            }
        }

        if(raw != 0 && j["spender"].is_string()) {
            try {
                const Json::Value tx = vertcoind->getrawtransaction(j["spender"].get<string>(), false);
                j["spenderRaw"] = tx.asString();
                j["spender"] = nullptr;
            } catch(const jsonrpc::JsonRpcException& e) {
                const std::string message(e.what());
                session->close(400, message, {{"Content-Type","text/plain"},{"Content-Length",  std::to_string(message.size())}});
                cout << "Not found " << message << endl;
                return;
            }
        }




    }
   
    string body = j.dump();
     
    session->close( OK, body, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(body.size()) } } );
} 


void VtcBlockIndexer::HttpServer::outpointSpends( const shared_ptr< Session > session )
{
    const auto request = session->get_request( );
    size_t content_length = 0;
    content_length = request->get_header( "Content-Length", 0);
    
    
    
    session->fetch( content_length, [ request, this ]( const shared_ptr< Session > session, const Bytes & body )
    {
        const auto request = session->get_request( );
        int raw = stoi(request->get_query_parameter("raw","0"));
        int unconfirmed = stoi(request->get_query_parameter("unconfirmed","0"));
        

        string content =string(body.begin(), body.end());
        json output = json::array();
        json input = json::parse(content);
        if(!input.is_null()) {
            for (auto& txo : input) {
                if(txo.is_object() && txo["txid"].is_string() && txo["vout"].is_number()) {
                    stringstream txoId;
                    txoId << "txo-" << txo["txid"].get<string>() << "-" << setw(8) << setfill('0') << txo["vout"].get<int>() << "-spent";
                    cout << "Checking outpoint spent " << txoId.str() << endl;
            
                    json j;
                    j["txid"] = txo["txid"];
                    j["vout"] = txo["vout"];
                    j["error"] = false;
                    stringstream txBlockKey;
                    string txBlock;
                    txBlockKey << "tx-" << txo["txid"].get<string>() << "-block";
                    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), txBlockKey.str(), &txBlock);
                    if(!s.ok()) {
                        j["error"] = true;
                        j["errorDescription"] = "Transaction ID not found";
                    }
                    else 
                    {
                        string spentTx;
                        s = this->db->Get(leveldb::ReadOptions(), txoId.str(), &spentTx);
                        if(s.ok()) {
                            j["spender"] = spentTx.substr(64, 64);
                            j["spent"] = true;
                            string blockHeightStr;
                            stringstream blockHashId;
                            blockHashId << "block-hash-" << spentTx.substr(0,64);
                            s = this->db->Get(leveldb::ReadOptions(), blockHashId.str(), &blockHeightStr);
                            if(s.ok()) {
                                j["height"] = stol(blockHeightStr);
                            }   
                        } else if(unconfirmed != 0) {
                            string mempoolSpend = mempoolMonitor->outpointSpend( txo["txid"].get<string>(), txo["vout"].get<int>());
                            if(mempoolSpend.compare("") != 0) {
                                json j;
                                j["spender"] = mempoolSpend;
                                j["spent"] = true;
                                j["height"] = 0;
                            } else {
                                j["spent"] = false;
                            }
                        }
                    }

                    if(raw != 0 && j["spender"].is_string()) {
                        try {
                            const Json::Value tx = vertcoind->getrawtransaction(j["spender"].get<string>(), false);
                            j["spenderRaw"] = tx.asString();
                            j["spender"] = nullptr;
                        } catch(const jsonrpc::JsonRpcException& e) {
                            const std::string message(e.what());
                            cout << "Not found " << message << endl;
                        }
                    }

                    output.push_back(j);
                    
                }
            }
        }
    
        string resultBody = output.dump();
        session->close( OK, resultBody, { { "Content-Type",  "application/json" }, { "Content-Length",  std::to_string(resultBody.size()) } } );
    } );
} 

void VtcBlockIndexer::HttpServer::sendRawTransaction( const shared_ptr< Session > session )
{
    const auto request = session->get_request( );
    const size_t content_length = request->get_header( "Content-Length", 0);
    session->fetch( content_length, [ request, this ]( const shared_ptr< Session > session, const Bytes & body )
    {
        const string rawtx = string(body.begin(), body.end());
        
        try {
            const auto txid = vertcoind->sendrawtransaction(rawtx);
            
            session->close(OK, txid, {{"Content-Type","text/plain"}, {"Content-Length",  std::to_string(txid.size())}});
        } catch(const jsonrpc::JsonRpcException& e) {
            const std::string message(e.what());
            session->close(400, message, {{"Content-Type","text/plain"},{"Content-Length",  std::to_string(message.size())}});
        }
    });
} 

void VtcBlockIndexer::HttpServer::run()
{
    auto addressBalanceResource = make_shared< Resource >( );
    addressBalanceResource->set_path( "/addressBalance/{address: .*}" );
    addressBalanceResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressBalance, this, std::placeholders::_1) );

    auto addressTxosResource = make_shared< Resource >( );
    addressTxosResource->set_path( "/addressTxos/{address: .*}" );
    addressTxosResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressTxos, this, std::placeholders::_1) );

    auto addressTxosSinceBlockResource = make_shared< Resource >( );
    addressTxosSinceBlockResource->set_path( "/addressTxosSince/{sinceBlock: ^[0-9]*$}/{address: .*}" );
    addressTxosSinceBlockResource->set_method_handler( "GET", bind( &VtcBlockIndexer::HttpServer::addressTxos, this, std::placeholders::_1) );
    
    auto getTransactionResource = make_shared<Resource>();
    getTransactionResource->set_path( "/getTransaction/{id: [0-9a-f]*}" );
    getTransactionResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getTransaction, this, std::placeholders::_1) );

    auto getTransactionProofResource = make_shared<Resource>();
    getTransactionProofResource->set_path( "/getTransactionProof/{id: [0-9a-f]*}" );
    getTransactionProofResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getTransactionProof, this, std::placeholders::_1) );

    auto outpointSpendResource = make_shared<Resource>();
    outpointSpendResource->set_path( "/outpointSpend/{txid: .*}/{vout: .*}" );
    outpointSpendResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::outpointSpend, this, std::placeholders::_1) );

    auto outpointSpendsResource = make_shared<Resource>();
    outpointSpendsResource->set_path( "/outpointSpends" );
    outpointSpendsResource->set_method_handler("POST", bind(&VtcBlockIndexer::HttpServer::outpointSpends, this, std::placeholders::_1) );

    auto sendRawTransactionResource = make_shared<Resource>();
    sendRawTransactionResource->set_path( "/sendRawTransaction" );
    sendRawTransactionResource->set_method_handler("POST", bind(&VtcBlockIndexer::HttpServer::sendRawTransaction, this, std::placeholders::_1) );

    auto blocksResource = make_shared<Resource>();
    blocksResource->set_path( "/blocks" );
    blocksResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getBlocks, this, std::placeholders::_1) );

    auto blockResource = make_shared<Resource>();
    blockResource->set_path( "/block/{hash: [0-9a-f]*}" );
    blockResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getBlock, this, std::placeholders::_1) );

    auto blockTransactionResource = make_shared<Resource>();
    blockTransactionResource->set_path( "/blocktxs/{hash: [0-9a-f]*}/{page: [0-9]*}" );
    blockTransactionResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getBlockTransactions, this, std::placeholders::_1) );

    auto blocksByDateResource = make_shared<Resource>();
    blocksByDateResource->set_path( "/blocksbydate" );
    blocksByDateResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::getBlocksByDate, this, std::placeholders::_1) );

    auto mempoolResource = make_shared<Resource>();
    mempoolResource->set_path( "/mempool" );
    mempoolResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::mempoolTransactionIds, this, std::placeholders::_1) );


    auto syncResource = make_shared<Resource>();
    syncResource->set_path( "/sync" );
    syncResource->set_method_handler("GET", bind(&VtcBlockIndexer::HttpServer::sync, this, std::placeholders::_1) );




    auto settings = make_shared< Settings >( );
    settings->set_port( 8888 );
    settings->set_default_header( "Connection", "close" );
    settings->set_default_header( "Access-Control-Allow-Origin", "*" );
    
    Service service;
    service.publish( addressBalanceResource );
    service.publish( addressTxosResource );
    service.publish( addressTxosSinceBlockResource );
    service.publish( getTransactionResource );
    service.publish( getTransactionProofResource );
    service.publish( outpointSpendResource );
    service.publish( outpointSpendsResource );
    service.publish( sendRawTransactionResource );
    service.publish( blockResource );
    service.publish( blockTransactionResource );
    service.publish( blocksResource );
    service.publish( blocksByDateResource );
    service.publish( mempoolResource );
    service.publish( syncResource );
    service.start( settings );
}
