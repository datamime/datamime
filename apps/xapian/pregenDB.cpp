#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include <thread>
#include <libxml/xmlreader.h>

#include "genDB.h"
#include "rapidjson/document.h"
#include "getopt.h"

using namespace std;

void usage() {
    cout << "Usage: pregenDB -d db -s path_to_src_db_file -n numdocs -l minDocLen -u maxDocLen" << endl;
    exit(-1);
}

void genStackOverflowDB(string dbPath, string srcPath, int numDocs,
                        int minDocLen, int maxDocLen) {
    Xapian::WritableDatabase db(dbPath, Xapian::DB_CREATE_OR_OVERWRITE);
    // Increase commit threshold to improve indexing throughput
    setenv("XAPIAN_FLUSH_THRESHOLD", "100000", 1);

    Xapian::TermGenerator termgen;
    Xapian::Stem stemmer("english");
    Xapian::SimpleStopper stopper;
    const char* stopWords[] = { "a", "about", "an", "and", "are", "as", "at", "be",
        "by", "en", "for", "from", "how", "i", "in", "is", "it", "of", "on",
        "or", "that", "the", "this", "to", "was", "what", "when", "where",
        "which", "who", "why", "will", "with" };

    stopper = Xapian::SimpleStopper(stopWords, \
            stopWords + sizeof(stopWords) / sizeof(stopWords[0]));

    termgen.set_stemmer(stemmer);
    termgen.set_stopper(&stopper);

    // Assume XML Document
    xmlTextReaderPtr reader;
    int ret;
    int idx = 0;
	const char* filename = srcPath.c_str();
    reader = xmlNewTextReaderFilename(filename);
    const xmlChar *bodyattr = xmlCharStrdup("Body");
    if (reader != NULL) {
        ret = xmlTextReaderRead(reader);
        while (ret == 1) {
            if (xmlTextReaderNodeType(reader) == 1) {
                unsigned char* xmlbody = xmlTextReaderGetAttribute(reader, bodyattr);
                if (xmlbody) {
                    string text(reinterpret_cast<char*>(xmlbody));
                    if ((text.size() >= (unsigned int)minDocLen) && (text.size() <= (unsigned int)maxDocLen)) {
                        Xapian::Document doc;
                        doc.set_data(text);
                        termgen.set_document(doc);
                        termgen.index_text(text);
                        db.add_document(doc);

                        if (++idx % 10000 == 0) {
                            db.commit();
                        }
                        if (idx > numDocs)
                            break;
                    }
                }
            }

            ret = xmlTextReaderRead(reader);
        }
        xmlFreeTextReader(reader);
        if (ret != 0 && idx <= numDocs)
            printf("%s : failed to parse\n", filename);
        else
            printf("Finished generating nd=%d,mindl=%d,maxdl=%d\n",
                    numDocs, minDocLen, maxDocLen);
    } else {
        printf("Unable to open %s\n", filename);
    }
}


int main(int argc, char* argv[]) {
    string dbPath = "db";
    string srcPath = "srcPath";
    string optString = "d:s:n:l:u:";
    int numDocs = 0, minDocLen = 0, maxDocLen = 0;
    int c;
    if (argc != 11) usage();
    while ((c = getopt(argc, argv, optString.c_str())) != -1) {
        switch (c) {
            case 'd':
                if(strcmp(optarg, "?") == 0) {
                    cerr << "Missing database" << endl;
                    usage();
                }
                dbPath = optarg;
                break;

            case 's':
                if(strcmp(optarg, "?") == 0) {
                    cerr << "Missing path to source file from which to generate DBs" << endl;
                    usage();
                }
                srcPath = optarg;
                break;

            case 'n':
                numDocs = atoi(optarg);
                break;

            case 'l':
                minDocLen = atoi(optarg);
                break;

            case 'u':
                maxDocLen = atol(optarg);
                break;

            default:
                cerr << "Unknown option " << c << endl;
                usage();
                exit(-1);
                break;
        }
    }

    printf("Starting DB generation\n");
    string dbPath_root = dbPath;
    genStackOverflowDB(dbPath, srcPath, numDocs, minDocLen, maxDocLen);
    /*
    for (numDocs =300000; numDocs <= 500000; numDocs += 100000) {
        std::vector<std::thread> threads;
        for (maxDocLen = 1000; maxDocLen <= 10000; maxDocLen += 1000) {
            for (logBtreeNodeSize = 11; logBtreeNodeSize < 17; logBtreeNodeSize++) {
                dbPath = dbPath_root + "/nd" + to_string(numDocs)
                    + "_mdl" + to_string(maxDocLen)
                    + "_bts" + to_string(logBtreeNodeSize);
                cout << "Generating: " << dbPath << endl;
                //genStackOverflowDB(dbPath, numDocs, logBtreeNodeSize, maxDocLen);
                threads.push_back(
                    move(std::thread(genStackOverflowDB, dbPath, numDocs,
                                     logBtreeNodeSize, maxDocLen))
                );
            }
        }

        for (thread& th : threads) {
            if (th.joinable())
                th.join();
        }
    }
    */


    return 0;

}
