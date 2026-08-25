#include <iostream>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>

int main() {
    int fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY);
    if (fd < 0) {
        std::cerr << "Cannot open port" << std::endl;
        return 1;
    }
    
    // Configure serial port
    struct termios attrs;
    tcgetattr(fd, &attrs);
    
    // Set 115200 baud
    cfsetispeed(&attrs, B115200);
    cfsetospeed(&attrs, B115200);
    
    // Set 8N1
    attrs.c_cflag = (attrs.c_cflag & ~CSIZE) | CS8;
    attrs.c_cflag &= ~PARENB;
    attrs.c_cflag &= ~CSTOPB;
    attrs.c_cflag |= CREAD | CLOCAL;
    
    // Raw mode
    attrs.c_iflag = 0;
    attrs.c_oflag = 0;
    attrs.c_lflag = 0;
    
    // Timeout
    attrs.c_cc[VMIN] = 0;
    attrs.c_cc[VTIME] = 20;  // 2 second timeout
    
    tcsetattr(fd, TCSANOW, &attrs);
    tcflush(fd, TCIOFLUSH);
    
    // Send GET_INFO
    unsigned char cmd[] = {0x57, 0xAB, 0x00, 0x01, 0x00, 0x03};
    std::cout << "Sending: ";
    for (auto b : cmd) printf("%02X ", b);
    std::cout << std::endl;
    
    write(fd, cmd, sizeof(cmd));
    
    // Wait for response
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    
    int ready = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ready > 0) {
        unsigned char resp[64];
        int n = read(fd, resp, sizeof(resp));
        std::cout << "Response (" << n << " bytes): ";
        for (int i = 0; i < n; i++) printf("%02X ", resp[i]);
        std::cout << std::endl;
        
        if (n >= 14 && resp[0] == 0x57 && resp[1] == 0xAB && resp[3] == 0x81) {
            std::cout << "✓✓✓ SUCCESS ✓✓✓" << std::endl;
        }
    } else {
        std::cout << "No response" << std::endl;
    }
    
    close(fd);
    return 0;
}
