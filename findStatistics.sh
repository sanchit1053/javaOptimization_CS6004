echo "finding $1 in $2"
sed -En "/$1/,+3p" $2

