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
#include "blockindexer.h"
#include "scriptsolver.h"
#include "blockchaintypes.h"
#include <iostream>
#include <sstream>

//#include "hashing.h"
#include <memory>
#include <iomanip>
#include <unordered_map>


using namespace std;

// This map keeps the nextTxoIndex in memory for speed - no database fetching on every TX
unordered_map<string, int> nextTxoIndex;



VtcBlockIndexer::BlockIndexer::BlockIndexer(const shared_ptr<leveldb::DB> db, const shared_ptr<VtcBlockIndexer::MempoolMonitor> mempoolMonitor) {
    this->db = db;
    this->mempoolMonitor = mempoolMonitor;
    this->scriptSolver = make_unique<VtcBlockIndexer::ScriptSolver>();
}


int VtcBlockIndexer::BlockIndexer::getNextTxoIndex(string prefix) {
    if(nextTxoIndex.find(prefix) == nextTxoIndex.end()) {
        leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
        nextTxoIndex[prefix] = 1;
        string start(prefix + "-00000001");
        string limit(prefix + "-99999999");
        
        for (it->Seek(start);
                it->Valid() && it->key().ToString() < limit;
                it->Next()) {
                    nextTxoIndex[prefix]++;
        }
        assert(it->status().ok());  // Check for any errors found during the scan
        delete it;
    } else {
        nextTxoIndex[prefix]++;    
    }
    
    return nextTxoIndex[prefix];
}

bool VtcBlockIndexer::BlockIndexer::clearBlockTxos(string blockHash) {
    leveldb::WriteBatch batch;
    
    string start(blockHash + "-txo-00000001");
    string limit(blockHash + "-txo-99999999");
    leveldb::Iterator* it = this->db->NewIterator(leveldb::ReadOptions());
    for (it->Seek(start);
            it->Valid() && it->key().ToString() < limit;
            it->Next()) {
        batch.Delete(it->value().ToString());           
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;

    string spentStart(blockHash + "-txospent-00000001");
    string spentLimit(blockHash + "-txospent-99999999");
    it = this->db->NewIterator(leveldb::ReadOptions());
    for (it->Seek(spentStart);
            it->Valid() && it->key().ToString() < spentLimit;
            it->Next()) {
        batch.Delete(it->value().ToString());           
    }
    assert(it->status().ok());  // Check for any errors found during the scan
    delete it;

    leveldb::Status s = this->db->Write(leveldb::WriteOptions(), &batch);
    return s.ok();
}

bool VtcBlockIndexer::BlockIndexer::hasIndexedBlock(string blockHash, int blockHeight)
{
    stringstream ss;
    ss << "block-" << setw(8) << setfill('0') << blockHeight;

    string existingBlockHash;
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), ss.str(), &existingBlockHash);
    if(s.ok() && existingBlockHash == blockHash) {
        return true;
    }
    
    return false;
}

bool VtcBlockIndexer::BlockIndexer::indexBlock(Block block) {
    //cout << "Indexing block " << block.blockHash << " (Height " << block.height << ")" << endl;
    
    
    stringstream ss;
    ss << "block-" << setw(8) << setfill('0') << block.height;
    
    string existingBlockHash;
    leveldb::Status s = this->db->Get(leveldb::ReadOptions(), ss.str(), &existingBlockHash);

    if(s.ok() && existingBlockHash == block.blockHash) {
        // Block found in database and matches. This block is indexed already, so skip.
        return true;
    } else if (s.ok()) {
        // There was a different block at this height. Ditch the TXOs from the old block.
        clearBlockTxos(existingBlockHash);
    }

    stringstream blockHeight;
    blockHeight << setw(8) << setfill('0') << block.height;

    string highestBlock;
    s = this->db->Get(leveldb::ReadOptions(), "highestblock", &highestBlock);
    if(!s.ok()) {
        this->db->Put(leveldb::WriteOptions(), "highestblock", blockHeight.str());
    } else {
        if(stoull(highestBlock) < block.height) {
            this->db->Put(leveldb::WriteOptions(), "highestblock", blockHeight.str());
        }
    }
    
    leveldb::WriteBatch batch;
    batch.Put(ss.str(), block.blockHash);

    
    stringstream ssBlockFilePositionKey;
    ssBlockFilePositionKey << "block-filePosition-" << setw(8) << setfill('0') << block.height;
    stringstream ssBlockFilePositionValue;
    ssBlockFilePositionValue << block.fileName << setw(12) << setfill('0') << block.filePosition;

  
    batch.Put(ssBlockFilePositionKey.str(), ssBlockFilePositionValue.str());

    
    stringstream ssBlockHashHeightKey;
    ssBlockHashHeightKey << "block-hash-" << block.blockHash;
    stringstream ssBlockHashHeightValue;
    ssBlockHashHeightValue << setw(8) << setfill('0') << block.height;

    batch.Put(ssBlockHashHeightKey.str(), ssBlockHashHeightValue.str());
    
    stringstream ssBlockTimeHeightKey;
    ssBlockTimeHeightKey << "block-time-" << setw(8) << setfill('0') << block.height;
    batch.Put(ssBlockTimeHeightKey.str(), std::to_string(block.time));
    
    stringstream ssBlockHeightTimeKey;
    ssBlockHeightTimeKey << "block-hash-time-" << setw(12) << setfill('0') << block.time;
    batch.Put(ssBlockHeightTimeKey.str(), block.blockHash);

    stringstream ssBlockSizeHeightKey;
    ssBlockSizeHeightKey << "block-size-" << setw(8) << setfill('0') << block.height;
    batch.Put(ssBlockSizeHeightKey.str(), std::to_string(block.byteSize));
    
    stringstream ssBlockTxCountHeightKey;
    ssBlockTxCountHeightKey << "block-txcount-"  << setw(8) << setfill('0') << block.height;
    batch.Put(ssBlockTxCountHeightKey.str(), std::to_string(block.transactions.size()));

    int txIndex = -1;
    // TODO: Verify block integrity
    for(VtcBlockIndexer::Transaction tx : block.transactions) {
        txIndex++;
        stringstream blockTxKey;
        blockTxKey << "block-" << block.blockHash << "-tx-" << setw(8) << setfill('0') << txIndex;
        batch.Put(blockTxKey.str(), tx.txHash);

        stringstream ssTxFilePositionKey;
        ssTxFilePositionKey << "tx-filePosition-" << tx.txHash;
        stringstream ssTxFilePositionValue;
        ssTxFilePositionValue << block.fileName << setw(12) << setfill('0') << tx.filePosition;
    
        batch.Put(ssTxFilePositionKey.str(), ssTxFilePositionValue.str());

        stringstream txBlockKey;
        txBlockKey << "tx-" << tx.txHash << "-block";
        batch.Put(txBlockKey.str(), block.blockHash);

        for(VtcBlockIndexer::TransactionOutput out : tx.outputs) {
            vector<string> addresses = this->scriptSolver->getAddressesFromScript(out.script);
            if(addresses.size() > 1) {
                if(scriptSolver->isMultiSig(out.script)) {
                    stringstream txoMultiSigKey;
                    txoMultiSigKey << "multisigtx-" << tx.txHash << "-" << setw(8) << setfill('0') << out.index;
                    batch.Put(txoMultiSigKey.str(), std::to_string(scriptSolver->requiredSignatures(out.script)));
                }
            }
        
            for(string address : addresses) {
                int nextIndex = getNextTxoIndex(address + "-txo");
                stringstream txoKey;
                txoKey << address << "-txo-" << setw(8) << setfill('0') << nextIndex;
                stringstream txoValue;
                txoValue << tx.txHash << setw(8) << setfill('0') << out.index << setw(8) << setfill('0') << block.height << out.value;
                batch.Put(txoKey.str(), txoValue.str());

                nextIndex = getNextTxoIndex(block.blockHash + "-txo");
                stringstream blockTxoKey;
                blockTxoKey << block.blockHash << "-txo-" << setw(8) << setfill('0') << nextIndex;
                batch.Put(blockTxoKey.str(), txoKey.str());


                stringstream txoAddressKey;
                txoAddressKey << tx.txHash << setw(8) << setfill('0') << out.index << "-address";
                nextIndex = getNextTxoIndex(txoAddressKey.str());
                txoAddressKey << "-" << setw(8) << setfill('0') << nextIndex;
                batch.Put(txoAddressKey.str(), address);
            }
            stringstream txoValueKey;
            txoValueKey << tx.txHash << setw(8) << setfill('0') << out.index << "-value";
            batch.Put(txoValueKey.str(), std::to_string(out.value));
        }

        for(VtcBlockIndexer::TransactionInput txi : tx.inputs) {
            if(!txi.coinbase)
            {
                stringstream txSpentKey;
                txSpentKey << "txo-" << txi.txHash << "-" << setw(8) << setfill('0') << txi.txoIndex << "-spent";
                
                stringstream spendingTx;
                spendingTx << block.blockHash << tx.txHash << setw(8) << setfill('0') << txi.index;
                
                batch.Put(txSpentKey.str(), spendingTx.str());

                int nextIndex = getNextTxoIndex(block.blockHash + "-txospent");
                stringstream blockTxoSpentKey;
                blockTxoSpentKey << block.blockHash << "-txospent-" << setw(8) << setfill('0') << nextIndex;
                batch.Put(blockTxoSpentKey.str(), txSpentKey.str());
            }
        }
        this->mempoolMonitor->transactionIndexed(tx.txHash);
    }

    
    this->db->Write(leveldb::WriteOptions(), &batch);

 

    return true;
}

