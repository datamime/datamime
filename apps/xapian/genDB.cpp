#include <iostream>
#include <fstream>
#include "genDB.h"

using namespace std;

void addPage(Xapian::WritableDatabase db, string title, string text) {
    try {
        Xapian::TermGenerator termgen;
        Xapian::Stem stemmer("english");
        termgen.set_stemmer(stemmer);

        Xapian::SimpleStopper stopper;
        const char* stopWords[] = { "a", "about", "an", "and", "are", "as", "at", "be",
            "by", "en", "for", "from", "how", "i", "in", "is", "it", "of", "on",
            "or", "that", "the", "this", "to", "was", "what", "when", "where",
            "which", "who", "why", "will", "with" };

        stopper = Xapian::SimpleStopper(stopWords, \
                stopWords + sizeof(stopWords) / sizeof(stopWords[0]));

        Xapian::Document doc;
        doc.set_data(text);
        termgen.set_document(doc);
        termgen.index_text(title);
        termgen.increase_termpos();
        termgen.index_text(text);

        db.add_document(doc);
    } catch(const Xapian::Error &e) {
        std::cout << "Error: " << e.get_description() << std::endl;
    }
}

