#ifndef __GENDB_H
#define __GENDB_H

#include <xapian.h>
#include <string>

void addPage(Xapian::WritableDatabase db, std::string title, std::string text);

#endif // __GENDB_H
