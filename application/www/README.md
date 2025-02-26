# HBox Web Config

Web configuration interface for HBox gamepad.

## Development

```bash
# Install dependencies
npm install

# Run development server
npm run dev
```

## Build & Flash

1. Generate web files:
```bash
# Build web interface
npm run build

# Generate firmware files
node makefsdata.js
```

2. Flash firmware:
- Build firmware: `make`
- Using STM32CubeProgrammer:
  - Flash `build/HBox.bin` to 0x08000000
  - Flash `Libs/httpd/ex_fsdata.bin` to 0x90000000

## Usage

1. Connect HBox via USB
2. Open http://192.168.7.1
3. Configure your device

## Deploy

```bash
# Deploy to server
cd www
./deploy.sh
```