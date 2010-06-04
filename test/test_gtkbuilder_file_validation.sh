#/bin/sh -e

which xmllint >/dev/null 2>&1
if [ $? -ne 0 ]; then
  echo "xmllint not installed. Please install it."
  exit 0
fi

for x in `find ../data/ -name "*.ui"`
do
  # TODO: Is there a GtkBuilder DTD?
  echo Validating $x
  xmllint --noout $x || exit 1
done
