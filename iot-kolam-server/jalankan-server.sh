#!/bin/bash

# Warna
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}     IoT Kolam Server - Laravel + Heartbeat     ${NC}"
echo -e "${BLUE}================================================${NC}"
echo ""

# Cek file artisan
if [ ! -f "artisan" ]; then
    echo -e "${YELLOW}[ERROR] Tidak ditemukan file artisan${NC}"
    exit 1
fi

PORT=${1:-8000}

echo -e "${GREEN}[INFO] Memulai server...${NC}"
echo -e "   📡 HTTP Server: ${BLUE}http://localhost:${PORT}${NC}"
echo -e "   💓 Heartbeat:   ${BLUE}Setiap 1 detik (iot:heartbeat)${NC}"
echo -e "   🛑 Tekan Ctrl+C untuk berhenti${NC}"
echo ""

# Trap Ctrl+C
cleanup() {
    echo ""
    echo -e "${YELLOW}Menghentikan server...${NC}"
    kill $SCHEDULER_PID 2>/dev/null
    kill $SERVER_PID 2>/dev/null
    exit 0
}
trap cleanup SIGINT SIGTERM

# Jalankan scheduler
php artisan schedule:work > storage/logs/scheduler.log 2>&1 &
SCHEDULER_PID=$!
echo -e "${GREEN}[OK] Scheduler berjalan (PID: $SCHEDULER_PID)${NC}"

sleep 2

# Jalankan server
php artisan serve --host=127.0.0.1 --port=$PORT &
SERVER_PID=$!
echo -e "${GREEN}[OK] HTTP Server berjalan (PID: $SERVER_PID)${NC}"
echo ""

# Tampilkan live log dari server
wait $SERVER_PID