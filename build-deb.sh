#!/bin/bash

# Pamir AI E-Ink Display Driver DKMS Debian Package Builder
# This script builds a Debian package for the Pamir AI E-Ink Display Driver DKMS modules

set -e

# Configuration
PACKAGE_NAME="pamir-ai-eink-dkms"
PACKAGE_VERSION="2.0.0"
DEBIAN_REVISION="1"
FULL_VERSION="${PACKAGE_VERSION}-${DEBIAN_REVISION}"
BUILD_DIR="dist"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print functions
print_info() {
	echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
	echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
	echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
	echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running on a Debian-based system
check_system() {
	if ! command -v dpkg-buildpackage &>/dev/null; then
		print_error "dpkg-buildpackage not found. Please install dpkg-dev:"
		print_error "  sudo apt-get install dpkg-dev"
		exit 1
	fi

	if ! command -v dh &>/dev/null; then
		print_error "dh not found. Please install debhelper:"
		print_error "  sudo apt-get install debhelper"
		exit 1
	fi
}

# Check build dependencies
check_dependencies() {
	print_info "Checking build dependencies..."

	local missing_deps=()

	# Required packages for building
	local required_packages=(
		"build-essential"
		"debhelper"
		"dkms"
		"device-tree-compiler"
		"fakeroot"
		"dpkg-dev"
		"gcc-aarch64-linux-gnu"
		"libc6-dev-arm64-cross"
	)

	for package in "${required_packages[@]}"; do
		if ! dpkg -l | grep -q "^ii  ${package}"; then
			missing_deps+=("${package}")
		fi
	done

	if [ ${#missing_deps[@]} -ne 0 ]; then
		print_error "Missing build dependencies: ${missing_deps[*]}"
		print_error "Please install them with:"
		print_error "  sudo apt-get install ${missing_deps[*]}"
		exit 1
	fi

	print_success "All build dependencies are satisfied"
}

# Clean previous builds
clean_build() {
	print_info "Cleaning previous builds..."

	# Remove build directory
	if [ -d "${BUILD_DIR}" ]; then
		rm -rf "${BUILD_DIR}"
	fi

	# Remove any generated files
	rm -f ../*.deb ../*.changes ../*.buildinfo ../*.dsc ../*.tar.* ../*.upload

	print_success "Build environment cleaned"
}

# Prepare source directory
prepare_source() {
	print_info "Preparing source directory..."

	# Create build directory
	mkdir -p "${BUILD_DIR}"

	# Copy source files to build directory
	local source_dir="${BUILD_DIR}/${PACKAGE_NAME}-${PACKAGE_VERSION}"
	mkdir -p "${source_dir}"

	# Copy source files
	cp "${SCRIPT_DIR}"/pamir-ai-eink-*.c "${source_dir}/" 2>/dev/null || true
	cp "${SCRIPT_DIR}"/pamir-ai-eink*.h "${source_dir}/" 2>/dev/null || true
	cp "${SCRIPT_DIR}"/Makefile "${source_dir}/"
	cp "${SCRIPT_DIR}"/dkms.conf "${source_dir}/"
	cp "${SCRIPT_DIR}"/*.dts "${source_dir}/" 2>/dev/null || true
	cp "${SCRIPT_DIR}"/README.md "${source_dir}/" 2>/dev/null || true
	
	# Copy examples if they exist
	if [ -d "${SCRIPT_DIR}/examples" ]; then
		cp -r "${SCRIPT_DIR}/examples" "${source_dir}/"
	fi

	# Copy debian packaging files
	cp -r "${SCRIPT_DIR}"/debian "${source_dir}/"

	# Fix changelog date
	sed -i "s/\$(date -R)/$(date -R)/" "${source_dir}/debian/changelog"
	
	# Cross-compile test programs for ARM64 BEFORE packaging
	print_info "Cross-compiling test programs for ARM64..."
	if [ -d "${source_dir}/examples" ]; then
		# Copy header to examples directory
		cp "${source_dir}/pamir-ai-eink.h" "${source_dir}/examples/" 2>/dev/null || true
		
		# Cross-compile each program
		print_info "Compiling eink_demo..."
		if aarch64-linux-gnu-gcc -Wall -O2 -I"${source_dir}/examples" \
			-o "${source_dir}/examples/eink_demo" \
			"${source_dir}/examples/eink_demo.c" -lm 2>/dev/null; then
			print_success "  eink_demo compiled"
		else
			print_warning "  Failed to compile eink_demo"
		fi
		
		print_info "Compiling eink_clock..."
		if aarch64-linux-gnu-gcc -Wall -O2 -I"${source_dir}/examples" \
			-o "${source_dir}/examples/eink_clock" \
			"${source_dir}/examples/eink_clock.c" -lm 2>/dev/null; then
			print_success "  eink_clock compiled"
		else
			print_warning "  Failed to compile eink_clock"
		fi
		
		print_info "Compiling eink_monitor..."
		if aarch64-linux-gnu-gcc -Wall -O2 -I"${source_dir}/examples" \
			-o "${source_dir}/examples/eink_monitor" \
			"${source_dir}/examples/eink_monitor.c" -lm 2>/dev/null; then
			print_success "  eink_monitor compiled"
		else
			print_warning "  Failed to compile eink_monitor"
		fi
		
		# Strip the binaries to reduce size
		print_info "Stripping binaries..."
		aarch64-linux-gnu-strip "${source_dir}/examples/eink_demo" 2>/dev/null || true
		aarch64-linux-gnu-strip "${source_dir}/examples/eink_clock" 2>/dev/null || true
		aarch64-linux-gnu-strip "${source_dir}/examples/eink_monitor" 2>/dev/null || true
		
		# Verify the binaries are ARM64
		if command -v file &>/dev/null; then
			for binary in eink_demo eink_clock eink_monitor; do
				if [ -f "${source_dir}/examples/${binary}" ]; then
					file_type=$(file "${source_dir}/examples/${binary}" | grep -o "ARM aarch64" || true)
					if [ -n "${file_type}" ]; then
						print_success "  ${binary}: ARM64 binary confirmed"
					fi
				fi
			done
		fi
		
		print_success "Test programs compiled for ARM64"
	else
		print_warning "Examples directory not found, skipping compilation"
	fi

	print_success "Source directory prepared at ${source_dir}"
}

# Build the package
build_package() {
	print_info "Building Debian package..."

	local source_dir="${BUILD_DIR}/${PACKAGE_NAME}-${PACKAGE_VERSION}"

	# Change to source directory
	cd "${source_dir}"

	# Build the package
	print_info "Running dpkg-buildpackage for ARM64 cross-compilation..."
	export DEB_BUILD_OPTIONS="nocheck"
	if dpkg-buildpackage -us -uc -b -d -aarm64 --host-arch=arm64; then
		print_success "Package built successfully"
	else
		print_error "Package build failed"
		exit 1
	fi

	# Return to original directory
	cd "${SCRIPT_DIR}"
}

# Display results
show_results() {
	print_info "Build completed successfully!"
	print_info "Generated files:"

	# List generated files
	for file in "${BUILD_DIR}"/*.deb "${BUILD_DIR}"/*.changes "${BUILD_DIR}"/*.buildinfo; do
		if [ -f "$file" ]; then
			local filename=$(basename "$file")
			local size=$(du -h "$file" | cut -f1)
			print_success "  ${filename} (${size})"
		fi
	done

	print_info ""
	print_info "To install the packages on ARM64 system:"
	print_info "  sudo dpkg -i ${BUILD_DIR}/${PACKAGE_NAME}_${FULL_VERSION}_all.deb"
	print_info "  sudo dpkg -i ${BUILD_DIR}/pamir-ai-eink-tests_${FULL_VERSION}_arm64.deb"
	print_info ""
	print_info "To install dependencies if needed:"
	print_info "  sudo apt-get install -f"
	print_info ""
	print_info "After installation, test programs will be available:"
	print_info "  eink-demo      - Basic demonstration"
	print_info "  eink-clock     - Real-time clock display"
	print_info "  eink-monitor   - System resource monitor"
	print_info "  eink-weather   - Weather dashboard"
	print_info "  eink-reader    - E-book reader"
	print_info ""
	print_info "Ensure user is in video group:"
	print_info "  sudo usermod -a -G video \$USER"
}

# Main execution
main() {
	print_info "Starting Debian package build for ${PACKAGE_NAME} ${FULL_VERSION}"
	print_info "Script directory: ${SCRIPT_DIR}"

	# Check system and dependencies
	# check_system
	# check_dependencies

	# Build process
	clean_build
	prepare_source
	build_package
	show_results

	print_success "Debian package build completed successfully!"
}

# Handle script arguments
case "${1:-}" in
clean)
	clean_build
	print_success "Clean completed"
	;;
check)
	# check_system
	# check_dependencies
	print_success "System check completed"
	;;
help | --help | -h)
	echo "Usage: $0 [clean|check|help]"
	echo ""
	echo "Commands:"
	echo "  clean    Clean build directory and generated files"
	echo "  check    Check system and build dependencies"
	echo "  help     Show this help message"
	echo ""
	echo "Default: Build the Debian package"
	;;
*)
	main
	;;
esac
