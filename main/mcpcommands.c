#include <stdint.h>
#include "driver/uart.h"

enum{
    LEVER_PULL_BACK = 0,
    LEVER_PUSH_FORWARDS = 1,
    LEVER_NOT_ACTUATED = 2
};


uint16_t crc16(uint8_t *packet, int nBytes) {
	uint16_t crc=0;
    for (int byte = 0; byte < nBytes; byte++) {
        crc = crc ^ ((uint16_t)packet[byte] << 8);
        for (unsigned char bit = 0; bit < 8; bit++) {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                    crc = crc << 1;
            }
        }
    }
    return crc;
}

void send_message(uint8_t command, uint8_t value){
	uint8_t send_buffer[5];
	uint16_t crc = 0;
	send_buffer[0] = 128;
	send_buffer[1] = command;
	send_buffer[2] = value;
	crc = crc16(send_buffer, 3);
	send_buffer[4] = crc;
	send_buffer[3] = crc >> 8;


	uart_write_bytes(UART_NUM_2, send_buffer, 5);

}


uint32_t read_encoder_value(uint8_t command){
    uart_flush_input(UART_NUM_2);
	uint8_t send_buffer [2];
	uint32_t encoder_count;
	send_buffer[0] = 128;
	send_buffer[1] = 16;
	uint8_t receive_buffer[7]; //uint32_t encoder count, uint8_t status bits, uint16_t crc





	uart_write_bytes(UART_NUM_2, send_buffer, 2);
    int length = 0;
    while (length < 7){
        uart_get_buffered_data_len(UART_NUM_2, (size_t*)&length);
    }
	length = uart_read_bytes(UART_NUM_2, receive_buffer, 7, 10);
	encoder_count = ((uint32_t)receive_buffer[0] << 24) | ((uint32_t)receive_buffer[1] << 16) | ((uint32_t)receive_buffer[2]<<8) | receive_buffer[3];
	return encoder_count;
}

void drive_M1(int accel, int speed, int deccel, int position){

	uint8_t send_buffer[2 + 4 + 4 + 4 + 4 + 2];
	uint16_t crc;
	send_buffer[0] = 128;
	send_buffer[1] = 65;

	send_buffer[2] = accel >> 24;
	send_buffer[3] = accel >> 16;
	send_buffer[4] = accel >> 8;
	send_buffer[5] = accel;

	send_buffer[6] = speed >> 24;
	send_buffer[7] = speed >> 16;
	send_buffer[8] = speed >> 8;
	send_buffer[9] = speed;

	send_buffer[10] = deccel >> 24;
	send_buffer[11] = deccel >> 16;
	send_buffer[12] = deccel >> 8;
	send_buffer[13] = deccel;

	send_buffer[14] = position >> 24;
	send_buffer[15] = position >> 16;
	send_buffer[16] = position >> 8;
	send_buffer[17] = position;

	crc = crc16(send_buffer, 18);

	send_buffer[18] = crc >> 8;
	send_buffer[19] = crc;

	uart_write_bytes(UART_NUM_2, send_buffer, 20);

}





void update_PID(float P_fp, float I_fp, float D_fp, uint32_t maxI, uint32_t deadzone, uint32_t minpos, uint32_t maxpos){
	uint8_t send_buffer[32];
	uint16_t crc = 0;

    uint32_t D = D_fp*1024;
    uint32_t P = P_fp*1024;
    uint32_t I = I_fp*1024;


	send_buffer[0] = 128;
	send_buffer[1] = 61;

	send_buffer[2] = D>>24;
	send_buffer[3] = D>>16;
	send_buffer[4] = D>>8;
	send_buffer[5] = D;

	send_buffer[6] = P>>24;
	send_buffer[7] = P>>16;
	send_buffer[8] = P>>8;
	send_buffer[9] = P;

	send_buffer[10] = I>>24;
	send_buffer[11] = I>>16;
	send_buffer[12] = I>>8;
	send_buffer[13] = I;

	send_buffer[14] = maxI>>24;
	send_buffer[15] = maxI>>16;
	send_buffer[16] = maxI>>8;
	send_buffer[17] = maxI;

	send_buffer[18] = deadzone>>24;
	send_buffer[19] = deadzone>>16;
	send_buffer[20] = deadzone>>8;
	send_buffer[21] = deadzone;

	send_buffer[22] = minpos>>24;
	send_buffer[23] = minpos>>16;
	send_buffer[24] = minpos>>8;
	send_buffer[25] = minpos;

	send_buffer[26] = maxpos>>24;
	send_buffer[27] = maxpos>>16;
	send_buffer[28] = maxpos>>8;
	send_buffer[29] = maxpos;

	crc = crc16(send_buffer, 30);

	send_buffer[30] = crc>>8;
	send_buffer[31] = crc;

	uart_write_bytes(UART_NUM_2, send_buffer, 32);
}


void reset_encoder_counter()
{
    
    uint8_t send_buffer[4];
    uint16_t crc;
    send_buffer[0] = 128;
    send_buffer[1] = 20;

    crc = crc16(send_buffer, 2);
    send_buffer[2] = crc >> 8;
    send_buffer[3] = crc;
    uart_write_bytes(UART_NUM_2, send_buffer, 4);

}


void read_PID(){
    uint8_t receive_buffer[30];

    uint8_t send_buffer[2];
    send_buffer[0] = 128;
    send_buffer[1] = 63;

    uart_write_bytes(UART_NUM_2, send_buffer, 2);
    uint32_t length = 0;



	length = uart_read_bytes(UART_NUM_2, receive_buffer, 30, 10);

    int P;
    int I;
    int D;
    int maxI;
    int deadzone;
    int minpos;
    int maxpos;



    P = ((uint32_t)receive_buffer[0])<<24 | ((uint32_t)receive_buffer[1])<<16 | ((uint32_t)receive_buffer[2])<<8 |((uint32_t)receive_buffer[3]);

    I = ((int)receive_buffer[4])<<24 | ((int)receive_buffer[5])<<16 | ((int)receive_buffer[6])<<8 |((int)receive_buffer[7]);





    float p_fp = ((float)P)/1024;
    float i_fp = ((float)I)/1024;
    printf("P: %f \n", p_fp);
    printf("I: %f \n", i_fp);
}


void set_position(uint32_t accel, uint32_t speed, uint32_t deccel, uint32_t position, uint8_t buffer)

{
    uint8_t send_buffer[21];
    uint16_t crc = 0;

    send_buffer[0] = 128;
    send_buffer[1] = 65;

    send_buffer[2] = accel >> 24;
    send_buffer[3] = accel >> 16;
    send_buffer[4] = accel >> 8;
    send_buffer[5] = accel;

    send_buffer[6] = speed >> 24;
    send_buffer[7] = speed >> 16;
    send_buffer[8] = speed >> 8;
    send_buffer[9] = speed;

    send_buffer[10] = deccel >> 24;
    send_buffer[11] = deccel >> 16;
    send_buffer[12] = deccel >> 8;
    send_buffer[13] = deccel;

    send_buffer[14] = position >> 24;
    send_buffer[15] = position >> 16;
    send_buffer[16] = position >> 8;
    send_buffer[17] = position;

    send_buffer[18] = buffer;

    crc = crc16(send_buffer, 19);

    send_buffer[19] = crc>>8;
    send_buffer[20] = crc;

    uart_write_bytes(UART_NUM_2, send_buffer, 21);



}

void read_mcp(uint8_t command, uint8_t* receive_buffer, uint8_t length){
    uint8_t send_buffer[2];

    send_buffer[0] = 128;
    send_buffer[1] = command;
    uart_flush_input(UART_NUM_2);

    uart_write_bytes(UART_NUM_2, send_buffer, 2);
    uart_read_bytes(UART_NUM_2, receive_buffer, length, 10);

    // printf("%d, %d, %d, %d \n", (int)receive_buffer[0], (int)receive_buffer[1], (int)receive_buffer[2], (int)receive_buffer[3]);


}


    uint8_t get_lever_state  (int thresh_position){
    uint8_t receive_buffer[7];
    uint8_t send_buffer[2];
    send_buffer[0] = 128;
    send_buffer[1] = 16;
    uart_flush_input(UART_NUM_2);
    uart_write_bytes(UART_NUM_2, send_buffer, 2);
    uart_read_bytes(UART_NUM_2, receive_buffer, 7, 10);

    int position;
    position = ((int)receive_buffer[0]) << 24 | ((int)receive_buffer[1]) << 16|((int)receive_buffer[2]) << 8 |(receive_buffer[3]);



    // printf("%d, %d, %d, %d \n", (int)receive_buffer[0], (int)receive_buffer[1], (int)receive_buffer[2], (int)receive_buffer[3]);



    uint8_t lever_state = 2;

    if (position>=thresh_position){
        lever_state = LEVER_PUSH_FORWARDS;
    } else if (position<= -thresh_position)
    {
        lever_state = LEVER_PULL_BACK;
    } else{
        lever_state = LEVER_NOT_ACTUATED;
    }



    // printf("position: %d \n", position);

    return lever_state;

}

void reset_position(){

    update_PID(600, 0.0, 0, 200, 10, -5000, 5000);
    set_position(800,100, 2000, 0, 1);

}

void unlock_lever(uint32_t range){
    update_PID(200, 0.03, 0, 200, range, -5000, -5000);
}







