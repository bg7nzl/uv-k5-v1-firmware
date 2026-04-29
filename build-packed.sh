#!/bin/sh

set -eu

usage() {
	cat <<'EOF'
Usage:
  ./build-packed.sh [options] [output-name] [MAKE_VAR=VALUE ...]

Examples:
  ./build-packed.sh
  ./build-packed.sh firmware-dsbtx ENABLE_DSB_TX=1
  ./build-packed.sh --rebuild-image firmware-test ENABLE_DTMF=1 ENABLE_DSB_TX=1

Options:
  -h, --help           Show this help text.
  -r, --rebuild-image  Rebuild the Docker image before building.
  -i, --image NAME     Override the Docker image name. Default: uvk5

The generated files are copied into compiled-firmware/.
EOF
}

quote_for_sh() {
	printf "'%s'" "$(printf "%s" "$1" | sed "s/'/'\"'\"'/g")"
}

image_name="${IMAGE_NAME:-uvk5}"
output_dir="${OUTPUT_DIR:-compiled-firmware}"
rebuild_image=0

while [ "$#" -gt 0 ]; do
	case "$1" in
		-h|--help)
			usage
			exit 0
			;;
		-r|--rebuild-image)
			rebuild_image=1
			shift
			;;
		-i|--image)
			if [ "$#" -lt 2 ]; then
				echo "Missing image name for $1" >&2
				exit 1
			fi
			image_name="$2"
			shift 2
			;;
		--)
			shift
			break
			;;
		-*)
			echo "Unknown option: $1" >&2
			usage >&2
			exit 1
			;;
		*)
			break
			;;
	esac
done

output_name="firmware"
if [ "$#" -gt 0 ]; then
	output_name="$1"
	shift
fi

if ! command -v docker >/dev/null 2>&1; then
	echo "docker is not installed or not in PATH" >&2
	exit 1
fi

mkdir -p "$output_dir"

if [ "$rebuild_image" -eq 1 ] || ! docker image inspect "$image_name" >/dev/null 2>&1; then
	docker build -t "$image_name" .
fi

container_cmd="set -e
rm -rf /tmp/uvk5-build
mkdir -p /tmp/uvk5-build
cp -a /src/. /tmp/uvk5-build/
cd /tmp/uvk5-build
make clean TARGET=$(quote_for_sh "$output_name") >/dev/null
make TARGET=$(quote_for_sh "$output_name")"

for arg in "$@"; do
	container_cmd="$container_cmd $(quote_for_sh "$arg")"
done

container_cmd="$container_cmd
cp $(quote_for_sh "$output_name") $(quote_for_sh "$output_name.bin") $(quote_for_sh "$output_name.packed.bin") /out/"

docker run --rm \
	--user "$(id -u):$(id -g)" \
	-v "$PWD:/src:ro" \
	-v "$PWD/$output_dir:/out" \
	"$image_name" \
	/bin/sh -lc "$container_cmd"

printf 'Generated %s/%s.packed.bin\n' "$output_dir" "$output_name"