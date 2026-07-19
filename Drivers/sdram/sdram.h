/*
 * sdram.h
 *
 *  Created on: Jul 19, 2026
 *      Author: Muhmmad Salman
 */

#ifndef SDRAM_H
#define SDRAM_H




typedef enum {
    SDRAM_OK    = 0,
    SDRAM_ERROR = 1,
} SDRAM_StatusTypeDef;

/*
Initialize the SDRAM calling SDRAM function internally
@return : SDRAM_StatusTypeDef
*/
SDRAM_StatusTypeDef SDRAM_Init(void);
/*
Test's SDRAM
@return : SDRAM_StatusTypeDef
*/
SDRAM_StatusTypeDef SDRAM_Test(void);



#endif
