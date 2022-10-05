#include <xapian.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include <iostream>
#include <fstream>

using namespace std;

void usage() {
    cout << "Usage: genterms -d db [ -f termsFile -u upperlimit -l lowerlimit -r]" << endl;
    exit(-1);
}

int main(int argc, char* argv[]) {

    char* dbPath = NULL;
    string termsFileName = "terms.in";
    unsigned llimit = 100;
    unsigned ulimit = 1000;
    bool createInRange = false;

    // Read command line options
    int c;
    string optString = "d:f:l:u:r"; // d: db, t: terms file, n: numThreads
    while ((c = getopt(argc, argv, optString.c_str())) != -1) {
        switch (c) {
            case 'd':
                if(strcmp(optarg, "?") == 0) {
                    cerr << "Missing database" << endl;
                    usage();
                }
                dbPath = optarg;
                break;

            case 'f':
                if(strcmp(optarg, "?") == 0) {
                    cerr << "Missing terms file" << endl;
                    usage();
                    exit(-1);
                }
                termsFileName = optarg;
                break;

            case 'l':
                if(strcmp(optarg, "?") == 0) {
                    cerr << "Missing mset upper limit file" << endl;
                    usage();
                    exit(-1);
                }
                llimit = stoul(optarg, nullptr, 10);
                break;

            case 'u':
                if(strcmp(optarg, "?") == 0) {
                    cerr << "Missing mset upper limit file" << endl;
                    usage();
                    exit(-1);
                }
                ulimit = stoul(optarg, nullptr, 10);
                break;

            case 'r':
                createInRange=true;
                break;

            default:
                cerr << "Unknown option: " << optopt << endl;
                exit(-1);

                break;
        }
    }

    Xapian::Database db;
    try {
        db.add_database(Xapian::Database(dbPath));
    }
    catch (const Xapian::Error& e) {
        cerr << "Error opening database: " << e.get_msg() << endl;
        usage();
    }

    Xapian::Enquire enquire(db);
    Xapian::Stem stemmer("english");
    Xapian::SimpleStopper stopper;
    const char* stopWords[] = { "a", "about", "an", "and", "are", "as", "at", "be",
        "by", "en", "for", "from", "how", "i", "in", "is", "it", "of", "on",
        "or", "that", "the", "this", "to", "was", "what", "when", "where",
        "which", "who", "why", "will", "with" };

    stopper = Xapian::SimpleStopper(stopWords, \
            stopWords + sizeof(stopWords) / sizeof(stopWords[0]));

    Xapian::QueryParser parser;
    parser.set_database(db);
    parser.set_default_op(Xapian::Query::OP_OR);
    parser.set_stemmer(stemmer);
    parser.set_stemming_strategy(Xapian::QueryParser::STEM_SOME);
    parser.set_stopper(&stopper);

    if (createInRange) {
        ofstream termsFiles[11 * 1000];
        cerr << "Generating terms up to llimit=1K, ulimit=100K" << endl;
        for (unsigned ll = 0; ll <= 1000; ll += 100) { // 11
            for (unsigned ul = 100; ul <= 100000; ul += 100) { // 1000
                stringstream ss;
                ss << "/gash/hrlee/terms/terms_ll" << ll << "_ul" << ul << ".in";
                ofstream& termsFile = termsFiles[(ll / 100) * 1000 + ((ul / 100) - 1)];
                termsFile.open(ss.str());
                if (termsFile.fail()) {
                    cerr << "Can't open terms file " << ss.str() << endl;
                    exit(-1);
                }
            }
        }
        cerr << "Opened output files" << endl;

        unsigned int MSET_SIZE = db.get_doccount();
        Xapian::MSet mset;
        unsigned long count = 0;

        string lowercase = "abcdefghijklmnopqrstuvwxyz";

        for (Xapian::TermIterator it = db.allterms_begin(); it != db.allterms_end(); it++) {
            Xapian::termcount cnt = db.get_collection_freq(*it);
            string term = *it;
            if ((term.find_first_of(lowercase) == 0) &&
                (term.find_first_not_of(lowercase) == string::npos)) {

                unsigned int flags = Xapian::QueryParser::FLAG_DEFAULT;
                Xapian::Query query = parser.parse_query(term, flags);
                enquire.set_query(query);
                mset = enquire.get_mset(0, MSET_SIZE);
                auto mset_size = mset.size();

                #pragma omp parallel for
                for (int i = 0; i < 11000; i++) {
                    unsigned ll = (i / 1000) * 100;
                    unsigned ul = ((i % 1000) * 100) + 100;
                    if (ll < ul && mset_size >= ll && mset_size <= ul)
                        termsFiles[i] << term << "," << cnt << endl;
                }
            }
            ++count;
            if (count % 100000 == 0) cerr << "count = " << count << endl;
        }

        for (int i = 0; i < 11000; i++)
            termsFiles[i].close();
    } else {
        // Open terms file
        ofstream termsFile(termsFileName);
        if (termsFile.fail()) {
            cerr << "Can't open terms file " << termsFileName << endl;
            exit(-1);
        }

        unsigned int MSET_SIZE = ulimit + 1;
        Xapian::MSet mset;
        unsigned long count = 0;

        string lowercase = "abcdefghijklmnopqrstuvwxyz";

        cerr << "Generating terms with llimit=" << llimit << ", ulimit=" << ulimit << endl;
        for (Xapian::TermIterator it = db.allterms_begin(); it != db.allterms_end(); it++) {
            Xapian::termcount cnt = db.get_collection_freq(*it);
            string term = *it;
            if ((term.find_first_of(lowercase) == 0) &&
                (term.find_first_not_of(lowercase) == string::npos)) {

                unsigned int flags = Xapian::QueryParser::FLAG_DEFAULT;
                Xapian::Query query = parser.parse_query(term, flags);
                enquire.set_query(query);
                mset = enquire.get_mset(0, MSET_SIZE);

                if (mset.size() >= llimit &&
                        mset.size() <= ulimit) {
                    termsFile << *it << "," << cnt << endl;
                }
            }
            ++count;
            //if (count % 100000 == 0) cerr << "count = " << count << endl;
        }

        termsFile.close();
    }

    return 0;
}
