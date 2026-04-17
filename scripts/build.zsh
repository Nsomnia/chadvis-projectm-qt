#!/usr/bin/env zsh
# ═══ ChadVis Build System v1337.2 ═══
# "Arch btw" Edition - Now with dual output and auto-install!
# 
# Refactored to use Zsh built-ins because external commands are for Juniors.
# ══════════════════════════════════════════════════════════════════════════

setopt ERR_EXIT PIPE_FAIL NO_UNSET EXTENDED_GLOB

# Load Chad modules
zmodload zsh/datetime
zmodload zsh/stat 2>/dev/null || true
zmodload zsh/parameter 2>/dev/null || true

# ─── Configuration ──────────────────────────────────────────────────────────

readonly SCRIPT_DIR="${0:A:h}"
readonly PROJECT_ROOT="${SCRIPT_DIR:h}"
readonly BUILD_DIR="${PROJECT_ROOT}/build"
readonly LOG_DIR="${PROJECT_ROOT}/logs"
readonly LOG_FILE="${LOG_DIR}/build.log"
readonly BINARY_NAME="chadvis-projectm-qt"
readonly BINARY_PATH="${BUILD_DIR}/${BINARY_NAME}"

# Log format options (for agentic workflows)
# TIMESTAMP_FORMAT: Controls timestamp format in log files
# - 'iso': ISO 8601 format (2024-01-15T10:30:45)
# - 'unix': Unix timestamp (1705315845)
# - 'human': Human-readable (2024-01-15 10:30:45) [default]
readonly TIMESTAMP_FORMAT="${CHADVIS_LOG_TIMESTAMP:-human}"

# Minimum libprojectm version required
readonly PROJECTM_MIN_VERSION="4.1.0"
readonly PROJECTM_GIT_TAG="v4.1.6"  # CPM version to use

# Hardware Detection (Zsh-native, no grep/sed/nproc bloat)
local cpu_cores
if (( ${+commands[getconf]} )); then
	cpu_cores=$(getconf _NPROCESSORS_ONLN)
else
	local cpuinfo=(${(f)"$(< /proc/cpuinfo)"})
	cpu_cores=${#${(M)cpuinfo:#processor*}}
fi
readonly CPU_CORES=${cpu_cores:-4}

# Detect total memory in MB (for job optimization)
local total_mem_mb=4096
if [[ -f /proc/meminfo ]]; then
	local meminfo_content=${(f)"$(< /proc/meminfo)"}
	local mem_line=${(M)meminfo_content:#MemTotal*}
	if [[ -n "$mem_line" ]]; then
		# Extract number: "MemTotal:       3877452 kB" -> 3877452
		local mem_kb=${${mem_line##*[:space:]}%kB}
		[[ -n "$mem_kb" && "$mem_kb" == <-> ]] && total_mem_mb=$(( mem_kb / 1024 ))
	fi
fi
readonly TOTAL_MEM_MB=${total_mem_mb}

# Optimize jobs based on CPU and memory
# For 2-core systems with <8GB RAM, use 1 job to avoid OOM
# For 4+ cores with 8GB+, use cores-1 for safety
# Rule: ~2GB RAM per compile job needed for C++
local optimal_jobs
if (( CPU_CORES <= 2 )); then
# Potato mode: 1 job, system is too constrained
optimal_jobs=1
elif (( TOTAL_MEM_MB < 4096 )); then
# Low memory: 1 job regardless of cores
optimal_jobs=1
elif (( TOTAL_MEM_MB < 8192 )); then
# Medium memory: half cores
optimal_jobs=$(( CPU_CORES / 2 ))
optimal_jobs=$(( optimal_jobs < 1 ? 1 : optimal_jobs ))
else
# Good memory: cores - 1 (leave 1 for system)
optimal_jobs=$(( CPU_CORES - 1 ))
fi
readonly OPTIMAL_JOBS=${optimal_jobs}

local cpu_model="Unknown Chad CPU"
if [[ -f /proc/cpuinfo ]]; then
	local model_line=${(M)${(f)"$(< /proc/cpuinfo)"}:#model name*}
	cpu_model=${model_line#*: }
fi
readonly CPU_MODEL=${cpu_model}

# ─── Visuals (ANSI Escape Codes) ───────────────────────────────────────────

# Colors
readonly C_RESET=$'\e[0m'
readonly C_BOLD=$'\e[1m'
readonly C_RED=$'\e[31m'
readonly C_GREEN=$'\e[32m'
readonly C_YELLOW=$'\e[33m'
readonly C_BLUE=$'\e[34m'
readonly C_MAGENTA=$'\e[35m'
readonly C_CYAN=$'\e[36m'
readonly C_GRAY=$'\e[90m'

# Icons
readonly ICON_INFO="${C_BLUE}󰋽${C_RESET}"
readonly ICON_OK="${C_GREEN}󰄬${C_RESET}"
readonly ICON_WARN="${C_YELLOW}󱈸${C_RESET}"
readonly ICON_ERROR="${C_RED}󰅚${C_RESET}"
readonly ICON_CHAD="${C_CYAN}󰭹${C_RESET}"

# ─── Helper Functions ───────────────────────────────────────────────────────

log() {
	local type=$1
	local msg=$2
	case $type in
		info) print -P "${ICON_INFO} ${msg}" ;;
		ok) print -P "${ICON_OK} ${msg}" ;;
		warn) print -P "${ICON_WARN} ${msg}" ;;
		error) print -P "${ICON_ERROR} ${C_BOLD}${msg}${C_RESET}" >&2 ;;
		header) print -P "\n${C_CYAN}═══ ${C_BOLD}${msg}${C_RESET}${C_CYAN} ═══${C_RESET}" ;;
		chad) print -P "${ICON_CHAD} ${C_CYAN}${C_BOLD}${msg}${C_RESET}" ;;
	esac
}

# Extract and display errors/warnings from log file for agentic workflows
# Usage: extract_build_errors <log_file> [count]
# Outputs structured format: ERROR: <file>:<line>: <message>
#                             WARNING: <file>:<line>: <message>
extract_build_errors() {
	local log_file="$1"
	local count="${2:-20}"

	[[ -f "$log_file" ]] || return 1

	# Extract CMake/Compiler errors and warnings with context
	# Patterns: error:, warning:, Error:, Warning:, ERROR:, WARNING:
	# Also catch CMake errors like "CMake Error at"
	local -a errors=()
	local -a warnings=()

	# Read log and categorize
	local line content
	while IFS= read -r line; do
		# Skip timestamp prefix if present [YYYY-MM-DD HH:MM:SS]
		content="${line#\[*\] }"

		# Check for errors (case-insensitive check using lowercase conversion)
		local lc_content="${(L)content}"
		if [[ "$lc_content" == *error:* || "$lc_content" == *cmake\ error* || "$lc_content" == *fatal\ error* || "$lc_content" == *undefined\ reference* ]]; then
			errors+=("$content")
		# Check for warnings (case-insensitive)
		elif [[ "$lc_content" == *warning:* || "$lc_content" == *note:* ]]; then
			warnings+=("$content")
		fi
	done < "$log_file"

	# Output summary for agentic parsing
	if (( ${#errors[@]} > 0 )); then
		print -P "\n${ICON_ERROR} ${C_RED}${C_BOLD}ERRORS (${#errors[@]}):${C_RESET}"
		local i=0
		for line in "${errors[@]}"; do
			print "${C_RED}ERROR:${C_RESET} $line"
			(( i++ >= count )) && break
		done
	fi

	if (( ${#warnings[@]} > 0 )); then
		print -P "\n${ICON_WARN} ${C_YELLOW}${C_BOLD}WARNINGS (${#warnings[@]}):${C_RESET}"
		local i=0
		for line in "${warnings[@]}"; do
			print "${C_YELLOW}WARNING:${C_RESET} $line"
			(( i++ >= count )) && break
		done
	fi

	# Return non-zero if errors found (for scripting)
	(( ${#errors[@]} == 0 ))
}

# Compare two version strings (returns 0 if v1 >= v2, 1 otherwise)
version_compare() {
    local v1="$1"
    local v2="$2"
    
    # Normalize versions (remove 'v' prefix if present)
    v1="${v1#v}"
    v2="${v2#v}"
    
    local -a v1_parts=(${(s/./)v1})
    local -a v2_parts=(${(s/./)v2})
    
    local max_len=$(( ${#v1_parts[@]} > ${#v2_parts[@]} ? ${#v1_parts[@]} : ${#v2_parts[@]} ))
    
    for (( i = 1; i <= max_len; i++ )); do
        local p1=${v1_parts[i]:-0}
        local p2=${v2_parts[i]:-0}
        
        if (( p1 > p2 )); then
            return 0  # v1 >= v2
        elif (( p1 < p2 )); then
            return 1  # v1 < v2
        fi
    done
    
    return 0  # versions are equal
}

# Get installed libprojectm version from pacman
get_system_projectm_version() {
    local version=""
    
    # Try pkg-config first
    if (( ${+commands[pkg-config]} )); then
        version=$(pkg-config --modversion projectM-4 2>/dev/null)
        [[ -n "$version" ]] && { echo "$version"; return 0 }
        
        version=$(pkg-config --modversion libprojectM 2>/dev/null)
        [[ -n "$version" ]] && { echo "$version"; return 0 }
    fi
    
    # Try pacman
    if (( ${+commands[pacman]} )); then
        local pacman_info=$(pacman -Q libprojectm 2>/dev/null)
        if [[ -n "$pacman_info" ]]; then
            # Extract version from "libprojectm 4.x.x-x"
            version="${pacman_info#libprojectm }"
            version="${version%%-*}"
            [[ -n "$version" ]] && { echo "$version"; return 0 }
        fi
    fi
    
    return 1
}

# Check if libprojectm is installed and meets version requirements
check_projectm_status() {
    local current_version=$(get_system_projectm_version)
    
    if [[ -z "$current_version" ]]; then
        echo "missing"
        return 0
    fi
    
    if version_compare "$current_version" "$PROJECTM_MIN_VERSION"; then
        echo "ok:$current_version"
        return 0
    else
        echo "outdated:$current_version"
        return 0
    fi
}

log_kv() {
    print -P "${C_GRAY}  │${C_RESET} ${C_BOLD}${1}:${C_RESET} ${2}"
}

human_size() {
    [[ -f "$1" ]] || { echo "N/A"; return; }
    local size
    if zstat -A size +size "$1" 2>/dev/null; then
        # Zsh-native size formatting
        if (( size > 1024 * 1024 * 1024 )); then
            printf "%.2f GiB" $(( size / 1024.0 / 1024.0 / 1024.0 ))
        elif (( size > 1024 * 1024 )); then
            printf "%.2f MiB" $(( size / 1024.0 / 1024.0 ))
        elif (( size > 1024 )); then
            printf "%.2f KiB" $(( size / 1024.0 ))
        else
            echo "${size} B"
        fi
    else
        echo "?"
    fi
}

# Run command with output to both console and log file
# Usage: run_logged [--clear] <log_file> <command> [args...]
# Captures BOTH stdout AND stderr completely, preserving order and colors
# Environment: CHADVIS_LOG_TIMESTAMP controls timestamp format in logs
# - 'human': Human-readable (2024-01-15 10:30:45) [default]
# - 'iso': ISO 8601 format (2024-01-15T10:30:45)
# - 'unix': Unix timestamp (1705315845)
# - 'none': No timestamp (raw output)
#
# Options:
# --clear Clear the log file before writing (default: append)
# --header Add a section header to the log before output
run_logged() {
	local clear_log=false
	local add_header=false

	# Parse options
	while [[ "$1" == --clear || "$1" == --header ]]; do
		case "$1" in
			--clear) clear_log=true; shift ;;
			--header) add_header=true; shift ;;
		esac
	done

	local log_file="$1"
	shift

	# Ensure log directory exists
	mkdir -p "$(dirname "$log_file")" 2>/dev/null || true

	# Clear log file if requested (only for first command in sequence)
	if [[ "$clear_log" == true ]]; then
		: > "$log_file"
	fi

	# Add header if requested
	if [[ "$add_header" == true ]]; then
		local header_ts
		case "${TIMESTAMP_FORMAT:-human}" in
			iso) header_ts=$(strftime '%Y-%m-%dT%H:%M:%S') ;;
			unix) header_ts=$(strftime '%s') ;;
			human) header_ts=$(strftime '%Y-%m-%d %H:%M:%S') ;;
			none) header_ts="" ;;
			*) header_ts=$(strftime '%Y-%m-%d %H:%M:%S') ;;
		esac
		local cmd_str="$*"
		if [[ -n "$header_ts" ]]; then
			printf '\n[%s] ═══ Running: %s ═══\n\n' "$header_ts" "$cmd_str" >> "$log_file"
		else
			printf '\n═══ Running: %s ═══\n\n' "$cmd_str" >> "$log_file"
		fi
	fi

	# Use a simple tee-based approach - robust and works everywhere
	# We use a temp file to add timestamps while tee handles console output
	local raw_output=$(mktemp --tmpdir="${TMPDIR:-/tmp}" chadvis-XXXXXX.log)
	local exit_status=0

	# Run command, capture all output (stdout + stderr) to temp file and console
	# Use unbuffer to preserve colors if available, otherwise direct
	if (( ${+commands[unbuffer]} )); then
		unbuffer "$@" 2>&1 | tee "$raw_output"
		exit_status=${pipestatus[1]}
	else
		"$@" 2>&1 | tee "$raw_output"
		exit_status=${pipestatus[1]}
	fi

	# Add timestamps to log file based on TIMESTAMP_FORMAT
	local ts_prefix=""
	case "${TIMESTAMP_FORMAT:-human}" in
		iso)
			# ISO 8601 timestamps
			while IFS= read -r line; do
				ts_prefix=$(strftime '%Y-%m-%dT%H:%M:%S')
				printf '[%s] %s\n' "$ts_prefix" "$line"
			done < "$raw_output" >> "$log_file"
		;;
		unix)
			# Unix timestamps
			while IFS= read -r line; do
				ts_prefix=$(strftime '%s')
				printf '[%s] %s\n' "$ts_prefix" "$line"
			done < "$raw_output" >> "$log_file"
		;;
		human)
			# Human-readable timestamps
			while IFS= read -r line; do
				ts_prefix=$(strftime '%Y-%m-%d %H:%M:%S')
				printf '[%s] %s\n' "$ts_prefix" "$line"
			done < "$raw_output" >> "$log_file"
		;;
		none)
			# No timestamps - raw output
			cat "$raw_output" >> "$log_file"
		;;
		*)
			# Default to human-readable
			while IFS= read -r line; do
				ts_prefix=$(strftime '%Y-%m-%d %H:%M:%S')
				printf '[%s] %s\n' "$ts_prefix" "$line"
			done < "$raw_output" >> "$log_file"
		;;
	esac

	# Cleanup temp file
	rm -f "$raw_output"

	return $exit_status
}

show_help() {
    log chad "ChadVis Build System — Help"
    print -P "${C_BOLD}Usage:${C_RESET} build.zsh [options] [command]"
    print -P "\n${C_BOLD}Options:${C_RESET}"
    print -P "  -h, --help              Show this message (if you're lost)"
    print -P "  -d, --debug             Build in Debug mode (for when you break things)"
    print -P "  -r, --release           Build in Release mode (default, for Chads)"
    print -P "  -c, --clean             Clean build directory before building"
    print -P "  -i, --install           Install after successful build"
    print -P "  -j, --jobs <n>          Number of parallel jobs (default: ${CPU_CORES})"
    print -P "  -y, --yes               Auto-install missing dependencies without prompting"
    print -P "      --system-projectm   Force use of system libprojectm via pacman"
    print -P "      --cpm-projectm      Force build libprojectm from source via CPM"
    print -P "\n${C_BOLD}Commands:${C_RESET}"
    print -P "  build            Configure and compile (default)"
    print -P "  clean            Remove build artifacts"
    print -P "  run              Build and run the application"
    print -P "  test             Run the test suite"
    print -P "  check-deps       Verify system dependencies"
    print -P "\n${C_BOLD}Examples:${C_RESET}"
    print -P "  ./build.sh -c -d build   # Clean, debug build"
    print -P "  ./build.sh -y build      # Auto-install deps if missing"
    print -P "  ./build.sh --cpm-projectm build  # Use CPM libprojectm"
}

# ─── Logic ──────────────────────────────────────────────────────────────────

mkdir -p "$LOG_DIR"

# Parse arguments using zparseopts (Zsh-native)
local -A opts
zparseopts -D -A opts h -help d -debug r -release c -clean i -install j: -jobs: y -yes \
    -system-projectm -cpm-projectm

if [[ -n "${opts[(i)-h]}" || -n "${opts[(i)--help]}" ]]; then
    show_help
    exit 0
fi

BUILD_TYPE="Release"
[[ -n "${opts[(i)-d]}" || -n "${opts[(i)--debug]}" ]] && BUILD_TYPE="Debug"
[[ -n "${opts[(i)-r]}" || -n "${opts[(i)--release]}" ]] && BUILD_TYPE="Release"

# Use optimal jobs by default, but allow override
JOBS=${opts[-j]:-${opts[--jobs]:-$OPTIMAL_JOBS}}
CLEAN_FIRST=false
[[ -n "${opts[(i)-c]}" || -n "${opts[(i)--clean]}" ]] && CLEAN_FIRST=true

INSTALL_AFTER=false
[[ -n "${opts[(i)-i]}" || -n "${opts[(i)--install]}" ]] && INSTALL_AFTER=true

AUTO_INSTALL=false
[[ -n "${opts[(i)-y]}" || -n "${opts[(i)--yes]}" ]] && AUTO_INSTALL=true

# libprojectm source preference
PROJECTM_SOURCE="auto"  # auto, system, cpm
[[ -n "${opts[(i)--system-projectm]}" ]] && PROJECTM_SOURCE="system"
[[ -n "${opts[(i)--cpm-projectm]}" ]] && PROJECTM_SOURCE="cpm"

COMMAND=${1:-build}

# ─── Commands ───────────────────────────────────────────────────────────────

cmd_check_deps() {
    log header "Dependency Check"
    
    local is_arch=false
    [[ -f /etc/arch-release ]] && is_arch=true

    # Check libprojectm status first (special handling)
    local projectm_status=$(check_projectm_status)
    local projectm_handled=false
    local use_cpm_projectm=false
    
    log info "libprojectm source preference: ${C_BOLD}${PROJECTM_SOURCE}${C_RESET}"
    
    case "$PROJECTM_SOURCE" in
        cpm)
            log info "Using CPM libprojectm (${PROJECTM_GIT_TAG}) as requested"
            use_cpm_projectm=true
            projectm_handled=true
            ;;
        system)
            if [[ "$projectm_status" == "missing" ]]; then
                log error "System libprojectm not found, but --system-projectm requested"
                if [[ "$is_arch" == true && "$AUTO_INSTALL" == true ]]; then
                    log info "Auto-installing libprojectm from pacman..."
                    if sudo pacman -S --needed libprojectm; then
                        log ok "libprojectm installed successfully"
                        use_cpm_projectm=false
                        projectm_handled=true
                    else
                        log error "Failed to install libprojectm"
                        return 1
                    fi
                else
                    log info "Install with: sudo pacman -S --needed libprojectm"
                    return 1
                fi
            elif [[ "$projectm_status" == outdated:* ]]; then
                local current_version="${projectm_status#outdated:}"
                log warn "System libprojectm (${current_version}) is older than minimum (${PROJECTM_MIN_VERSION})"
                log info "Consider using --cpm-projectm for latest version"
                use_cpm_projectm=false
                projectm_handled=true
            else
                local current_version="${projectm_status#ok:}"
                log ok "Using system libprojectm ${current_version}"
                use_cpm_projectm=false
                projectm_handled=true
            fi
            ;;
        auto)
            if [[ "$projectm_status" == "missing" ]]; then
                log warn "libprojectm not found on system"
                if [[ "$is_arch" == true ]]; then
                    if [[ "$AUTO_INSTALL" == true ]]; then
                        log info "Auto-installing libprojectm from pacman..."
                        if sudo pacman -S --needed libprojectm; then
                            log ok "libprojectm installed from pacman"
                            use_cpm_projectm=false
                            projectm_handled=true
                        else
                            log warn "Pacman install failed, CMake will use CPM fallback"
                            use_cpm_projectm=true
                            projectm_handled=true
                        fi
                    else
                        log info "Will let CMake handle libprojectm (CPM fallback available)"
                        log info "Or run with -y to auto-install from pacman"
                        use_cpm_projectm=true
                        projectm_handled=true
                    fi
                else
                    log info "Non-Arch system, CMake will use CPM fallback"
                    use_cpm_projectm=true
                    projectm_handled=true
                fi
            elif [[ "$projectm_status" == outdated:* ]]; then
                local current_version="${projectm_status#outdated:}"
                log warn "System libprojectm (${current_version}) is outdated (min: ${PROJECTM_MIN_VERSION})"
                log info "CMake will use CPM to build newer version from source"
                use_cpm_projectm=true
                projectm_handled=true
            else
                local current_version="${projectm_status#ok:}"
                log ok "System libprojectm ${current_version} meets requirements"
                use_cpm_projectm=false
                projectm_handled=true
            fi
            ;;
    esac

    # Expanded dependency list - organized by category
    local -a deps=(
        # Build tools
        "cmd:cmake:cmake" "cmd:ninja:ninja" "cmd:g++:gcc"
        # Qt6 base packages
        "pkg:Qt6Core:qt6-base" "pkg:Qt6Widgets:qt6-base" "pkg:Qt6OpenGLWidgets:qt6-base"
        "pkg:Qt6Network:qt6-base" "pkg:Qt6Sql:qt6-base"
        # Qt6 multimedia (separate package)
        "pkg:Qt6Multimedia:qt6-multimedia"
        # Qt6 extras
        "pkg:Qt6Svg:qt6-svg"
        "pkg:Qt6WebEngine:qt6-webengine"
        # Core libraries (excluding libprojectm if handled via CPM)
        "pkg:libavcodec:ffmpeg" "pkg:libavformat:ffmpeg"
        "pkg:libavutil:ffmpeg" "pkg:libswresample:ffmpeg"
        # Other dependencies
        "pkg:spdlog:spdlog" "pkg:fmt:fmt" "pkg:taglib:taglib"
        "pkg:sqlite3:sqlite" "pkg:glew:glew" "pkg:glm:glm"
    )
    
    # Add libprojectm to deps only if not using CPM
    if [[ "$use_cpm_projectm" == false ]]; then
        deps+=("pkg:projectM-4:libprojectm")
    fi
    
    local -a missing_pkgs=()
    local missing_count=0

    for spec in "${deps[@]}"; do
        local typ="${spec%%:*}"
        local rest="${spec#*:}"
        local chk="${rest%%:*}"
        local pkg="${rest#*:}"
        
        local found=false
        if [[ "$typ" == "cmd" ]]; then
            (( ${+commands[$chk]} )) && found=true
        else
            # Try pkg-config first
            pkg-config --exists "$chk" 2>/dev/null && found=true
            # Fallback for Arch: check pacman directly
            if [[ "$found" == false && "$is_arch" == true ]]; then
                pacman -Qq "$pkg" &>/dev/null && found=true
            fi
        fi

        if [[ "$found" == true ]]; then
            log ok "Found $chk"
        else
            log error "Missing: $chk (Package: $pkg)"
            missing_pkgs+=("$pkg")
            missing_count=$((missing_count + 1))
        fi
    done

    if (( ${#missing_pkgs[@]} > 0 )); then
        log warn "Missing dependencies detected!"
        
        if [[ "$is_arch" == true ]]; then
            # Remove duplicates from missing packages
            local -a unique_missing=(${(u)missing_pkgs})
            
            print -P "\n${C_BOLD}Packages to install:${C_RESET} ${(j: :)unique_missing}"
            print -P "${C_GRAY}Install command: sudo pacman -S --needed ${(j: :)unique_missing}${C_RESET}"
            
            if [[ "$AUTO_INSTALL" == true ]]; then
                log info "Auto-install enabled. Installing missing packages..."
                if sudo pacman -S --needed "${unique_missing[@]}"; then
                    log ok "Dependencies installed successfully!"
                    # Re-check to confirm
                    cmd_check_deps
                    return $?
                else
                    log error "Failed to install dependencies"
                    return 1
                fi
            else
                print -P "\n${C_YELLOW}Run with -y flag to auto-install:${C_RESET} ./build.sh -y $COMMAND"
                print -P "${C_YELLOW}Or install manually:${C_RESET} sudo pacman -S --needed ${(j: :)unique_missing}"
            fi
        else
            log warn "Auto-install only supported on Arch Linux."
            log info "Please install the missing packages manually."
        fi
    fi
    
    # Export CPM preference for CMake
    if [[ "$use_cpm_projectm" == true ]]; then
        export CHADVIS_CPM_PROJECTM=1
    else
        unset CHADVIS_CPM_PROJECTM
    fi

    return $missing_count
}

cmd_build() {
    local start_time=$EPOCHSECONDS
    
    # Auto-check deps before building, because we're Chads
    cmd_check_deps || { 
        log error "Dependencies missing. Fix them, Junior."
        log info "Hint: Run './build.sh -y build' to auto-install on Arch."
        return 1 
    }

	log header "Building ChadVis"
	log_kv "CPU" "$CPU_MODEL"
	log_kv "Cores" "$CPU_CORES"
	log_kv "Memory" "${TOTAL_MEM_MB}MB"
	log_kv "Type" "$BUILD_TYPE"
	log_kv "Jobs" "$JOBS (optimal: $OPTIMAL_JOBS)"
	log_kv "Log" "$LOG_FILE"

    if [[ "$CLEAN_FIRST" == true ]]; then
        cmd_clean
    fi

    mkdir -p "$BUILD_DIR"

	log info "Configuring with CMake..."
	log info "Output: Console + $LOG_FILE"

	# Build CMake arguments
	# Optimize flags based on build type
	local cmake_cxx_flags=""
	if [[ "$BUILD_TYPE" == "Debug" ]]; then
		cmake_cxx_flags="-O0 -g"
	else
		# Release: Use -O2 for faster compilation than -O3, with LTO for size
		cmake_cxx_flags="-O2"
	fi

	local cmake_args=(
		-G Ninja
		-DCMAKE_BUILD_TYPE="$BUILD_TYPE"
		-DCMAKE_CXX_COMPILER_LAUNCHER=sccache
		-DCMAKE_CXX_FLAGS="$cmake_cxx_flags"
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
		-S "$PROJECT_ROOT"
		-B "$BUILD_DIR"
	)
    
    # Pass CPM preference to CMake if set (check environment variable set by cmd_check_deps)
    if [[ -n "${CHADVIS_CPM_PROJECTM:-}" ]]; then
        log info "Passing CPM libprojectm preference to CMake"
        cmake_args+=(-DCHADVIS_FORCE_CPM_PROJECTM=ON)
    fi
    
	# Run cmake with output to console and log file
	# Clear log at start and add header for this build step
	if ! run_logged --clear --header "$LOG_FILE" cmake "${cmake_args[@]}"; then
		log error "CMake configuration failed!"
		log info "Check $LOG_FILE for full details."
		extract_build_errors "$LOG_FILE" 10
		return 1
	fi

    log info "Compiling with Ninja ($JOBS jobs)..."
    log info "Output: Console + $LOG_FILE"
    
	# Run ninja with output to console and log file
	# Append to existing log and add header for this build step
	if ! run_logged --header "$LOG_FILE" ninja -C "$BUILD_DIR" -j "$JOBS"; then
		log error "Compilation failed!"
		log info "Check $LOG_FILE for full details."
		extract_build_errors "$LOG_FILE" 20
		return 1
	fi

    local end_time=$EPOCHSECONDS
    local duration=$(( end_time - start_time ))

    log ok "Build successful! (Took ${duration}s)"
    log_kv "Binary" "$BINARY_PATH"
    log_kv "Size" "$(human_size "$BINARY_PATH")"

    # Reminder for the Chads
    print -P "\n${ICON_CHAD} ${C_CYAN}${C_BOLD}REMINDER:${C_RESET} Did you update ${C_BOLD}CHANGELOG.md${C_RESET} for these major gains?"

    # Do not clear log file on success, user wants to see output if needed
    # : > "$LOG_FILE"
    
    if [[ "$INSTALL_AFTER" == true ]]; then
        cmd_install
    fi
}

cmd_clean() {
    log header "Cleaning"
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
        log ok "Build directory Krush Killed and Destryoed witta big ol' nuke!"
    else
        log info "Nothing to clean. You're already clean, Chad."
    fi
}

cmd_install() {
    log header "Installing"
    log info "Running installation (might need sudo)..."
    if ! run_logged --header "$LOG_FILE" sudo ninja -C "$BUILD_DIR" install; then
        log error "Installation failed!"
        log info "Check $LOG_FILE for full details."
        return 1
    fi
    log ok "Installed successfully. Go forth and visualize."
}

cmd_run() {
    if [[ ! -x "$BINARY_PATH" ]]; then
        log warn "Binary not found. Building first..."
        cmd_build || return 1
    fi
    log header "Launching ChadVis"
    exec "$BINARY_PATH" "$@"
}

cmd_test() {
    log header "Running Tests"
    if [[ ! -d "$BUILD_DIR" ]]; then
        cmd_build || return 1
    fi
    # Assuming tests are built into build/tests/
    if [[ -d "$BUILD_DIR/tests" ]]; then
        run_logged --header "$LOG_FILE" ctest --test-dir "$BUILD_DIR" --output-on-failure
    else
        log warn "No tests found. Did you build them?"
    fi
}

# ─── Main ───────────────────────────────────────────────────────────────────

case $COMMAND in
    build)      cmd_build ;;
    clean)      cmd_clean ;;
    run)        cmd_run ;;
    test)       cmd_test ;;
    check-deps) cmd_check_deps ;;
    install)    cmd_install ;;
    *)          log error "Unknown command: $COMMAND"; show_help; exit 1 ;;
esac

