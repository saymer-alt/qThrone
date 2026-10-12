#!/usr/bin/env bash
set -euo pipefail
revision=74cac6d47bf4c27947348ee957538a2c0728a485
if [ ! -d openflux-source/.git ]; then
    python3 script/fetch_pinned_source.py https://github.com/p1neappleXpress/OpenFlux.git "$revision" openflux-source
fi
test "$(git -C openflux-source rev-parse HEAD)" = "$revision"
cp script/openflux/*.go openflux-source/
pushd openflux-source
CGO_ENABLED=0 "$GOCMD" build -trimpath -ldflags='-s -w' -o "$DEST/openflux$([[ "$GOOS" == windows ]] && echo '.exe')" .
cp LICENSE "$DEST/OpenFlux-LICENSE"
printf '%s\n' 'https://github.com/p1neappleXpress/OpenFlux' "Revision: $revision" 'Desktop integration: script/openflux in qThrone' > "$DEST/OpenFlux-SOURCE"
tar -czf "$DEST/openflux-source.tar.gz" --exclude=.git --exclude=.idea --exclude=ios-app --exclude=docker --exclude=.github .
popd
