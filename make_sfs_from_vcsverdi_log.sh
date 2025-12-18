#!/usr/bin/env bash
set -euo pipefail

log_file="${HOME}/vcsverdi.log"
out_dir="$(pwd)"
tmp_base=""

usage() {
  cat <<'USAGE'
Usage: make_sfs_from_vcsverdi_log.sh [options]

Options:
  -l <log_file>   Path to access log (default: ~/vcsverdi.log)
  -o <out_dir>    Output directory for .sfs files (default: current dir)
  -t <tmp_dir>    Temporary working directory (default: mktemp)
  -h              Show help

Produces:
  <out_dir>/vcs.sfs   contains bin/, etc/, linux64/, ... (root is /opt/vcs contents)
  <out_dir>/verdi.sfs contains bin/, etc/, platform/, ... (root is /opt/verdi contents)
USAGE
}

while getopts ":l:o:t:h" opt; do
  case "${opt}" in
    l) log_file="${OPTARG}" ;;
    o) out_dir="${OPTARG}" ;;
    t) tmp_base="${OPTARG}" ;;
    h) usage; exit 0 ;;
    *) usage; exit 2 ;;
  esac
done

if [[ ! -f "${log_file}" ]]; then
  echo "Error: log file not found: ${log_file}" >&2
  exit 1
fi

if ! command -v mksquashfs >/dev/null 2>&1; then
  echo "Error: mksquashfs not found in PATH" >&2
  exit 1
fi

mkdir -p "${out_dir}"

work_dir=""
if [[ -n "${tmp_base}" ]]; then
  mkdir -p "${tmp_base}"
  work_dir="$(mktemp -d "${tmp_base%/}/vcsverdi_sfs.XXXXXX")"
else
  work_dir="$(mktemp -d)"
fi

cleanup() {
  rm -rf "${work_dir}"
}
trap cleanup EXIT

stage_vcs="${work_dir}/stage_vcs"
stage_verdi="${work_dir}/stage_verdi"
mkdir -p "${stage_vcs}" "${stage_verdi}"

copy_one_under_prefix() {
  local prefix="$1"
  local src="$2"
  local dest_root="$3"
  local rel=""

  case "${src}" in
    "${prefix}"/*) rel="${src#"${prefix}/"}" ;;
    *) return 0 ;;
  esac

  if [[ -L "${src}" || -f "${src}" ]]; then
    ( cd "${prefix}" && cp -a --parents "${rel}" "${dest_root}" ) 2>/dev/null || true
  fi
}

vcs_count=0
verdi_count=0
missing_count=0

while IFS= read -r line || [[ -n "${line}" ]]; do
  p="${line%$'\r'}"
  [[ -z "${p}" ]] && continue

  case "${p}" in
    /opt/vcs/*)
      if [[ -e "${p}" || -L "${p}" ]]; then
        copy_one_under_prefix "/opt/vcs" "${p}" "${stage_vcs}"
        ((vcs_count++)) || true
      else
        ((missing_count++)) || true
      fi
      ;;
    /opt/verdi/*)
      if [[ -e "${p}" || -L "${p}" ]]; then
        copy_one_under_prefix "/opt/verdi" "${p}" "${stage_verdi}"
        ((verdi_count++)) || true
      else
        ((missing_count++)) || true
      fi
      ;;
    *)
      ;;
  esac

done < <(LC_ALL=C sort -u "${log_file}")

vcs_sfs="${out_dir%/}/vcs.sfs"
verdi_sfs="${out_dir%/}/verdi.sfs"

rm -f "${vcs_sfs}" "${verdi_sfs}"

if [[ -n "$(find "${stage_vcs}" -mindepth 1 -print -quit 2>/dev/null || true)" ]]; then
  mksquashfs "${stage_vcs}" "${vcs_sfs}" -noappend >/dev/null
else
  echo "Warning: no files staged for /opt/vcs; not creating ${vcs_sfs}" >&2
fi

if [[ -n "$(find "${stage_verdi}" -mindepth 1 -print -quit 2>/dev/null || true)" ]]; then
  mksquashfs "${stage_verdi}" "${verdi_sfs}" -noappend >/dev/null
else
  echo "Warning: no files staged for /opt/verdi; not creating ${verdi_sfs}" >&2
fi

echo "Done."
echo "  log:      ${log_file}"
echo "  out:      ${out_dir}"
echo "  staged:   vcs=${vcs_count} verdi=${verdi_count} missing=${missing_count}"
echo "  outputs:  ${vcs_sfs}  ${verdi_sfs}"
