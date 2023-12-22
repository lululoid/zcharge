#!/bin/bash

version=$1
versionCode=$2

# Check for decimal in arguments
for arg in "$@"; do
	if [[ $arg =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
		true
	else
		echo "> Arguments must be number"
		exit 1
	fi
done

last_version=$(grep -o 'version=v[0-9.]*' module.prop | cut -d'=' -f2 | sed 's/v//')

if [ -z "$version" ]; then
	version=$(grep -o 'version=v[0-9.]*' module.prop | cut -d'=' -f2 | sed 's/v//')
fi

if [ -z "$versionCode" ]; then
	versionCode=$(grep versionCode module.prop | cut -d "=" -f2)
	versionCode=$((versionCode + 1))
	if [ "$(echo "$version > $last_version" | bc -l)" -eq 1 ]; then
		first_two=$(echo "$versionCode" | sed -E 's/^([0-9]{2}).*/\1/')
		first_two=$((first_two + 1))
		versionCode=$(echo "$versionCode" | sed -E "s/[0-9]{2}(.*)/$first_two\1/")
	fi
fi

# U think I lazy to type? No, i just really forgetful sometimes
sed -i "s/\(version=v\)[0-9.]*\(-beta\)/\1$version\2/g; s/\(versionCode=\)[0-9]*/\1$versionCode/g" module.prop

changelog_file=$(find . -type f -iname "*changelog.md")
mv -f "$changelog_file" "zcharge-v${version}_$versionCode-beta-changelog.md"

7za a "packages/zcharge-v${version}_$versionCode-beta.zip" . \
	-x!*changelog.md \
	-x!wrapit.sh \
	-x!README.md \
	-x!packages \
	-x!.git \
  -x!test
