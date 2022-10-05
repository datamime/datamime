/** $lic$
 * Copyright (C) 2021-2022 by Massachusetts Institute of Technology
 *
 * This file is part of Datamime.
 *
 * This tool is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 3.
 *
 * If you use this software in your research, we request that you reference
 * the Datamime paper ("Datamime: Generating Representative Benchmarks by
 * Automatically Synthesizing Datasets", Lee and Sanchez, MICRO-55, October 2022)
 * as the source in any publications that use this software, and that you send
 * us a citation of your work.
 *
 * This tool is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <iostream>
#include <string>
#include <assert.h>
#include <thread>
#include <random>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <queue>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)

std::mutex mtx;
std::condition_variable cv;
std::queue<std::string> msgs;

// shamelessly stolen from StackOverflow but it does the right thing
uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

struct Node {
    Node* left;
    Node* right;
    int val;
    Node* parent;

    Node(int _val, Node* _parent) : val(_val), parent(_parent), left(nullptr),
        right(nullptr) {}

    bool is_leaf(){
        return (left == nullptr && right == nullptr);
    }
};

Node* build_tree(int val, int depth, Node* parent){
    assert(val % (1 << depth) == 0); // invariant

    auto* root = new Node(val, parent);

    if(depth == 0){
        assert(root->is_leaf());
        assert(val % 2 == 1); // leaves must be odd
        return root;
    }

    int diff = (1 << (depth - 1));
    root->left = build_tree(val - diff, depth - 1, root);
    root->right = build_tree(val + diff, depth - 1, root);
    return root;
}

void delete_if_leaf(Node* root, int key){
    if(!root) return;
    if(key == root->val){
        if(root->is_leaf()){
            if(!root->parent) return;
            if(root == root->parent->left){
                delete root->parent->left;
                root->parent->left = nullptr;
            } else {
                delete root->parent->right;
                root->parent->right = nullptr;
            }
        }
        return;
    } else if(key > root->val){
        delete_if_leaf(root->right, key);
    } else {
        delete_if_leaf(root->left, key);
    }
}

uint64_t do_stuff(int depth, int id){
    auto* root = build_tree(1 << depth, depth, nullptr);

    std::mt19937 generator(373);
    int max = (1 << (depth + 1)) - 1;
    std::uniform_int_distribution<int> distribution(1, max);

    uint64_t steps = 0;

    while(!root->is_leaf()){
        int key = distribution(generator);
        delete_if_leaf(root, key);
        steps++;
    }
    delete root;
    return steps;
}

void do_stuff_forever(int depth, int id){


    using namespace std::chrono;

    while(true){

        auto rdtsc_start = rdtsc();

        auto res = do_stuff(depth, id);

        auto rdtsc_stop = rdtsc();

        auto duration = rdtsc_stop - rdtsc_start;

        std::stringstream ss;
        ss << id << ", ";
        ss << rdtsc_start << ", " << rdtsc_stop << ", " << duration;

        //int random_sleep = rand() % 1000;
        //std::this_thread::sleep_for(milliseconds(random_sleep));

        mtx.lock();
        msgs.push(ss.str());
        cv.notify_one();
        mtx.unlock();
    }
}

void sleep_fn(){
    while(true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

int main(int argc, char* argv[]){
    if(argc < 2){
        std::cout << "Usage: ./microbenchmark <depth>\n";
        return -2;
    }

    int depth = std::stoi(argv[1]);

    std::vector<std::thread> thrs;

    for (int i = 0; i < 4; i++) {
        thrs.emplace_back(std::thread(do_stuff_forever, depth, i));
    }

    //std::thread sleeper(sleep_fn);

    while(true){
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, []{
                return (msgs.size() > 0);
        });

        while (msgs.size() > 0) {
            std::cout << msgs.front() << std::endl;
            msgs.pop();
        }
    }

    return 0;
}
