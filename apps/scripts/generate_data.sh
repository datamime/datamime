#!/bin/bash
# Requriements: p7zip, zstd
# NOTE: You need to build xapian and dnn first as this script uses helper scripts/programs
# in those app directories

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source ${DIR}/../configs.sh

if [ ! -d "$1" ]; then 
    echo "Directory $1 does not exist."
    exit
fi

echo "Generating relevant data at $1"

# dnn
mkdir $1/dnn
${DIR}/../dnn/scripts/create-model.py -A resnet50 -P $1 -r

# memcached and xapian
wget https://archive.org/download/stackexchange/stackoverflow.com-Posts.7z 
wget https://ftp.pdl.cmu.edu/pub/datasets/twemcacheWorkload/open_source/cluster27.0.zst
echo "Download complete"

mkdir $1/memcached
mkdir $1/xapian
p7zip -d stackoverflow.com-Posts.7z
zstd -d cluster27.0.zst
head -n 1000000000 cluster27.0 > $1/memcached/cluster27.0_firstbillion
rm cluster27.0.zst
rm stackoverflow.com-Posts.7z

# Pre-index possible xapian databases per document length and pre-generate all possible
# term distributions according to the bounds in params.py
mkdir $1/xapian/stackoverflow-dbs
mkdir $1/xapian/stackoverflow-dbs/terms
for doclen in {100..200..100}
do
    mkdir $1/xapian/stackoverflow-dbs/nd600000_avgdl$doclen
    mkdir $1/xapian/stackoverflow-dbs/terms/nd600000_avgdl$doclen
    lowerdoclen=$((doclen - 50))
    upperdoclen=$((doclen + 50))
    ${DIR}/../xapian/pregenDB -d $1/xapian/stackoverflow-dbs/nd600000_avgdl$doclen -s ${DIR}/Posts.xml -n 600000 -l $lowerdoclen -u $upperdoclen
    ${DIR}/../xapian/create_terms.py ${DIR}/../xapian/genTerms $1/xapian/stackoverflow-dbs/nd600000_avgdl$doclen $1/xapian/stackoverflow-dbs/terms/nd600000_avgdl$doclen
done

rm ${DIR}/Posts.xml
