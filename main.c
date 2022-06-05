//
//  main.c
//  MIDIReader
//
//  Created by Степан Мальцев on 29.04.21.
//

#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define errExit(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define ROW_COUNT 24
#define MAX_ITERATION 128

struct MIDI_Header {
    int8_t* section_name;
    uint32_t section_length;
    uint16_t mode;
    uint16_t channels;
    uint16_t time_settings;
};

struct MIDI_Note {
    uint8_t roomNotes;                    // Номер ноты[0-15].
    uint32_t noteTime;                    // Время в которое воспроизводится нота.
    uint8_t dynamicsNote;                 // Динамика взятия/отпускания ноты.
    uint8_t channelNote;                  // Канал ноты.
};

struct MIDI_Mtrk {
    int8_t* section_name;
    uint32_t section_length;
    struct MIDI_Note* notes_array;
    uint32_t array_size;
};

uint32_t read_reverse_int(int32_t fd) {
    uint32_t res = 0;
    for(uint32_t index = 4; index | 0x00000000; index--) {
        uint8_t byte = 0;
        read(fd, &byte, 1);
        res |= (uint32_t) (((uint32_t) byte) << 8 * (index - 1));
    }
    return res;
}

uint16_t read_reverse_short(int32_t fd) {
    uint16_t res = 0;
    for(int index = 2; index | 0x00000000; index--) {
        uint8_t byte = 0;
        read(fd, &byte, 1);
        res |= (uint16_t) (((uint16_t) byte) << 8 * (index - 1));
    }
    return res;
}

uint8_t read_byte(int32_t fd) {
    uint8_t res = 0;
    read(fd, &res, 1);
    return res;
}

int8_t* read_bytes(int32_t fd, uint32_t count) {
    int8_t* res = (int8_t*) malloc(count);
    read(fd, res, count);
    return res;
}

struct MIDI_Header read_midi_header(int32_t fd) {
    struct MIDI_Header header;
    header.section_name = read_bytes(fd, 4);
    header.section_length = read_reverse_int(fd);
    header.mode = read_reverse_short(fd);
    header.channels = read_reverse_short(fd);
    header.time_settings = read_reverse_short(fd);
    return header;
}

struct MIDI_Mtrk read_midi_blocks(int32_t fd) {
    struct MIDI_Mtrk mtrk;
    uint32_t array_size = 1;
    mtrk.notes_array = (struct MIDI_Note*) malloc(sizeof(struct MIDI_Note));
    struct MIDI_Note note;
    mtrk.section_name = read_bytes(fd, 4);
    mtrk.section_length = read_reverse_int(fd);
    uint32_t loop_index = mtrk.section_length;
    uint32_t real_time = 0;
    while(loop_index | 0x00000000) {
        uint8_t loop_count = 0;
        uint8_t buffer = 0;
        uint32_t buffer_time = 0;
        
        do {
            buffer = read_byte(fd);
            loop_count++;
            buffer_time <<= 7;
            buffer_time |= (uint8_t) (buffer & 0x7F);
        } while ((buffer & (1 << 7)) != 0);
        
        real_time += buffer_time;
        buffer = read_byte(fd);
        uint8_t cond = (uint8_t)buffer & 0xF0;
        loop_count++;
        if(buffer == 0xFF) {
            buffer = read_byte(fd); // Считываем номер мета-события
            buffer = read_byte(fd); // Считываем длину.
            loop_count += 2;
            for (uint32_t loop = 0; loop < buffer; loop++) {
                read_byte(fd);
            }
            loop_index -= loop_count + buffer; // Отнимаем от счетчика длинну считанного.
        } else switch(cond) {
            case 0xB0:
            case 0xC0:
            case 0xD0:
                read_byte(fd);
                loop_index -= loop_count + 1;
                break;
            case 0xE0:
            case 0x80: // снятие клавиш не записываем в массив нот
                read_bytes(fd, 2);
                loop_index -= loop_count + 2;
                break;
            case 0x90:
                note.channelNote = (uint8_t) (buffer & 0x0F); // Копируем номер канала.
                note.roomNotes = read_byte(fd); // Копируем номер ноты.
                note.dynamicsNote = read_byte(fd); // Копируем динамику ноты.
                note.noteTime = real_time; // Присваеваем реальное время ноты.
                mtrk.notes_array[array_size - 1] = note;
                mtrk.notes_array = (struct MIDI_Note*) realloc(mtrk.notes_array, ++array_size * sizeof(struct MIDI_Note));
                mtrk.array_size = array_size - 1;
                loop_index -= loop_count + 2; // Отнимаем прочитанное.
                break;
        }
    }
    return mtrk;
}

int main(int argc, const char * argv[]) {
    int32_t fd = open(argv[1], O_RDWR);
    if(fd < 0)
        errExit("MIDI file not found\n");
    struct stat sbuf;
    fstat(fd, &sbuf);
    struct MIDI_Header header = read_midi_header(fd);
    printf("MIDI block count = %d\n", header.channels);
    
    uint8_t record_state[ROW_COUNT][MAX_ITERATION];
    //uint8_t new_record[ROW_COUNT * MAX_ITERATION];
    memset(record_state, 0, ROW_COUNT * MAX_ITERATION);
    //memset(new_record, 0, ROW_COUNT * MAX_ITERATION);
    
    for(uint32_t i = 0; i < header.channels; i++) {
        struct MIDI_Mtrk mtrk = read_midi_blocks(fd);
        if(i == 0) // читаем только 1-й блок
            continue;
        if(mtrk.array_size | 0x00000000) {
            for(uint32_t k = 0; k < mtrk.array_size; k++) {
                record_state[mtrk.notes_array[k].roomNotes][mtrk.notes_array[k].noteTime / 24] = 1;
            }
        } else errExit("MIDI does not contain notes\n");
        break;
    }
    ftruncate(fd, ROW_COUNT * MAX_ITERATION); // очищаем MIDI файл чтобы записать полезную информацию
    lseek(fd, 0, SEEK_SET);
    //int offset = 0;
    for(int i = 0; i < 4; i++) {
        for(int k = 0; k < 4; k++) {
            for(int j = 0; j < 6; j++) {
                //memcpy(new_record + offset, record_state[j + i * 6] + k * 32, 32);
                //offset += 32;
                write(fd, record_state[j + i * 6] + k * 32, 32);
            }
        }
    }
    /*for(int i = 0; i < ROW_COUNT; i++) {
     for(int k = 0; k < MAX_ITERATION; k++) {
     printf("%3d", new_record[i * MAX_ITERATION + k]);
     }
     printf("\n");
     }*/
    //write(fd, new_record, ROW_COUNT * MAX_ITERATION);
    close(fd);
    return 0;
}
