#include "project.h"
#include "dfu.h"
#include "crc32.h"
#include <unistd.h>

struct ErrorDescription {
  int code;
  const char *description;
};

struct ErrorDescription errorDescriptions[] = {
  {0x00, "Invalid code: The provided opcode was missing or malformed."},
  {0x01, "Success: The operation completed successfully."},
  {0x02, "Opcode not supported: The provided opcode was invalid."},
  {0x03, "Invalid parameter: A parameter for the opcode was missing."},
  {0x04, "Insufficient resources: There was not enough memory for the data object."},
  {0x05, "Invalid object: The data object did not match the firmware and hardware requirements, the signature was missing, or parsing the command failed."},
  {0x07, "Unsupported type: The provided object type was not valid for a Create or Read operation."},
  {0x08, "Operation not permitted: The state of the DFU process did not allow this operation."},
  {0x0A, "Operation failed: The operation failed."},
  {0x0B, "Extended error."},
  {-1  , "Unknown code: ???"}
};

struct ErrorDescription extendedErrorDescriptions[] =
  {
   {0x00, "No extended error code has been set. This error indicates an implementation problem."},
   {0x01, "Invalid error code. This error code should  never be used outside of development."},
   {0x02, "The format of the command was incorrect. This error code is not used in the current implementation"},
   {0x03, "The command was successfully parsed, but it is not supported or unknown."},
   {0x04, "The init command is invalid. The init packet either has an invalid update type or it is missing required fields for the update type"},
   {0x05, "The firmware version is too low."},
   {0x06, "The hardware version of the device does not match the required hardware version for the update."},
   {0x07, "The array of supported SoftDevices for the update does not contain the FWID of the current SoftDevice"},
   {0x08, "The init packet does not contain a signature. This error code is not used in the current implementation, because init packets without a signature are regarded as invalid."},
   {0x09, "The hash type that is specified by the init packet is not supported by the DFU bootloader."},
   {0x0A, "The hash of the firmware image cannot be calculated."},
   {0x0B, "The type of the signature is unknown or not supported by the DFU bootloader."},
   {0x0C, "The hash of the received firmware image does not match the hash in the init packet."},
   {0x0D, "The available space on the device is insufficient to hold the firmware."},
   {-1  , "Unknown extended code: ???"}
  };


#define MAX_BLE_TX_SIZE (20)

void dfuPrintHumanReadableError(BLE *ble){
  int notifyCode, extendedNotifyCode;
  ble_getNotifyCodes(ble, &notifyCode, &extendedNotifyCode);

  
  struct ErrorDescription *errorDescription = errorDescriptions;
  while (errorDescription->code != -1){
    if (errorDescription->code == notifyCode){
      break;
    }
    errorDescription++;
  }
  printf("The operation failed \"%s (code: %d)\"\n", errorDescription->description, notifyCode);

  if (notifyCode == 0x0B){
    struct ErrorDescription *errorDescription = extendedErrorDescriptions;
    while (errorDescription->code != -1){
      if (errorDescription->code == extendedNotifyCode){
	break;
      }
      errorDescription++;
    }
    printf("%s (extended code: %d)\"\n", errorDescription->description, extendedNotifyCode);
  }
}

int dfuSendPackage(BLE * ble, uint8_t *packageData, size_t packageDataLength, BleObjType packageType){
  static uint8_t dontResume = 0;
  //Only active if non 0
  uint32_t debugCreateCRCError = 0;
  
  uint8_t buffer[MAX_BLE_PACKAGE_SIZE];
  int returnCode;
  //Number of bytes for the next BLE data transfer
  size_t chunkLength;
  
  uint8_t done = 0;

  //Number of bytes successfully send until now.
  uint32_t send=0;
  
  //The transfer block size as reported by "select"
  uint32_t blockSize;
  uint32_t returnedOffset;
  
  uint32_t returnedCRC32;
  uint32_t calculatedCRC32;

  uint32_t currentBlockIndex = 0;
  uint32_t transferSize;

  transferSize = packageDataLength;

  //Select the type of object to get information about the size of the buffer
  ble_wait_setup(ble, OP_CODE_SELECT);
  buffer[0]         = OP_CODE_SELECT;
  buffer[1]         = (uint8_t)packageType;
  ble_send_cp(ble, buffer, 2);
  returnCode = ble_wait_run(ble);
  if (returnCode != BLE_DFU_RESP_VAL_SUCCESS){
    dfuPrintHumanReadableError(ble);
    return returnCode;
  }
  if (ble->last_notification_package_size == 15 && ble->last_notification_package[1] == OP_CODE_SELECT){
    blockSize  = ble->last_notification_package[3+0]<<0;
    blockSize |= ble->last_notification_package[3+1]<<8;
    blockSize |= ble->last_notification_package[3+2]<<16;
    blockSize |= ble->last_notification_package[3+3]<<24;
    
    returnedOffset  = ble->last_notification_package[7+0]<<0;
    returnedOffset |= ble->last_notification_package[7+1]<<8;
    returnedOffset |= ble->last_notification_package[7+2]<<16;
    returnedOffset |= ble->last_notification_package[7+3]<<24;

    returnedCRC32   = ble->last_notification_package[11+0]<<0;
    returnedCRC32  |= ble->last_notification_package[11+1]<<8;
    returnedCRC32  |= ble->last_notification_package[11+2]<<16;
    returnedCRC32  |= ble->last_notification_package[11+3]<<24;

    printf("Start offset %u and CRC %u\n", returnedOffset, returnedCRC32);    
    printf("Transfer block size is %u bytes\n", blockSize);

    if (returnedOffset > 0){
      calculatedCRC32 = crc32_compute(packageData, returnedOffset, 0);
      if (calculatedCRC32 == returnedCRC32) {
	if ((packageType == BLE_OBJ_TYPE_COMMAND) && dontResume) {
	  printf ("Resume was aborted\n");
	}
	else {
	  printf("Data ok, resuming transmission\n");
	  send = returnedOffset;
	  //Request command execution to acknowledge the existing data
	  ble_wait_setup(ble, OP_CODE_EXECUTE);
	  buffer[0]         = OP_CODE_EXECUTE;
	  ble_send_cp(ble, buffer, 1);
	  returnCode = ble_wait_run(ble);
	  if (returnCode != BLE_DFU_RESP_VAL_SUCCESS){
	    dfuPrintHumanReadableError(ble);
	    if (returnCode == BLE_DFU_RESP_VAL_OPPERATION_NOT_PERMITTED){
	      printf("\n\n");
	      printf("=================================================\n");
	      printf("= To resolve this either power cycle the device =\n");
	      printf("=         or allow the DFU to timeout           =\n");
	      printf("=               and try again                   =\n");
	      printf("=================================================\n");
	      printf("\n\n");
	    }
	    dontResume = 1;
	    return returnCode;
	  }
	}
      }
      else {
	printf("Bad CRC for existing data!\n");
	if (packageType == BLE_OBJ_TYPE_DATA){
	  dontResume = 1;
	  return BLE_DFU_RESP_VAL_OPPERATION_FAILED;
	}
      }
    }

  }
  else {
    printf("Unexpected notification from the peripheral\n");
    return BLE_DFU_RESP_VAL_OPPERATION_FAILED;
  }
  if (transferSize>blockSize) { transferSize = blockSize; } //Start off with the maximum allowed (actually specifying using smaller transfers than this seems to fail)

  //Ensure that there is actually that much data left to send
  if (transferSize > packageDataLength - send){
    transferSize = packageDataLength - send;
  }
  
  if (transferSize!=0){
    //There are still data that needs to be send. (in case of a resumed transfer)
    //Create a new object
    ble_wait_setup(ble, OP_CODE_CREATE);
    buffer[0]         = OP_CODE_CREATE;
    buffer[1]         = (uint8_t)packageType;
    buffer[2]         = (transferSize>> 0) & 0xFF;
    buffer[3]         = (transferSize>> 8) & 0xFF;
    buffer[4]         = (transferSize>>16) & 0xFF;
    buffer[5]         = (transferSize>>24) & 0xFF;  
    ble_send_cp(ble, buffer, 6);
    returnCode = ble_wait_run(ble);
    if (returnCode != BLE_DFU_RESP_VAL_SUCCESS){
      dfuPrintHumanReadableError(ble);
      return returnCode;
    }
  }

  done = transferSize==0;
  
  while(!done){
     //Transfer data over BLE link
    printf("Sending bytes from %u of %lu\n", send, packageDataLength);
    
    currentBlockIndex = 0;
    while (send + currentBlockIndex < packageDataLength && currentBlockIndex<blockSize){
      chunkLength = MAX_BLE_TX_SIZE;
      //Handle partially filled buffers due to out of data
      if (chunkLength > packageDataLength - send - currentBlockIndex) {
	chunkLength = packageDataLength - send - currentBlockIndex;
      }
      
      //Handle partially filled buffers due to end of current block
      if (currentBlockIndex + chunkLength > blockSize){
	chunkLength = blockSize - currentBlockIndex;
      }

      memcpy(buffer, packageData+send+currentBlockIndex, chunkLength);
      if (debugCreateCRCError != 0 && (send + currentBlockIndex >= debugCreateCRCError)){
	printf("Creating an artificial crc error for debugging\n");
	debugCreateCRCError = 0;
	buffer[0] = ~buffer[0];
      }
      if (ble_send_data_noresp(ble, buffer, chunkLength)!= EXIT_SUCCESS){
	return BLE_DFU_RESP_VAL_OPPERATION_FAILED;
      }
      currentBlockIndex += chunkLength;
    }
    
    if (currentBlockIndex < blockSize){
      //less than a full block was send, sleep before requesting the checksum
      //This is done to work around a bug where a very short final transfer causes an error for the DFU update (when sending data fast)
      sleep(1);
    }
    
    //Request checksum
    ble_wait_setup(ble, OP_CODE_CALCULATE_CHECKSUM);
    buffer[0]         = OP_CODE_CALCULATE_CHECKSUM;
    ble_send_cp(ble, buffer, 1);
    returnCode = ble_wait_run(ble);
    if (returnCode != BLE_DFU_RESP_VAL_SUCCESS){
      dfuPrintHumanReadableError(ble);
      return returnCode;
    }
    if (ble->last_notification_package_size == 11 && ble->last_notification_package[1] == OP_CODE_CALCULATE_CHECKSUM){
      returnedOffset  = ble->last_notification_package[3+0]<<0;
      returnedOffset |= ble->last_notification_package[3+1]<<8;
      returnedOffset |= ble->last_notification_package[3+2]<<16;
      returnedOffset |= ble->last_notification_package[3+3]<<24;
      
      returnedCRC32   = ble->last_notification_package[7+0]<<0;
      returnedCRC32  |= ble->last_notification_package[7+1]<<8;
      returnedCRC32  |= ble->last_notification_package[7+2]<<16;
      returnedCRC32  |= ble->last_notification_package[7+3]<<24;

      //printf("checksum result %u of %u bytes\n", returnedCRC32, returnedOffset);
    }
    else {
      printf("Unexpected notification from the peripheral\n");
      return BLE_DFU_RESP_VAL_OPPERATION_FAILED;
    }

    if (returnedOffset>0){
      //It really always should be >0
      calculatedCRC32 = crc32_compute(packageData, returnedOffset, 0);
      if (calculatedCRC32 == returnedCRC32){
	send = returnedOffset;    
	if (send >= packageDataLength) {
	  //No more data left
	  done = 1;
	  //Request command execution to acknowledge the last data
	  ble_wait_setup(ble, OP_CODE_EXECUTE);
	  buffer[0]         = OP_CODE_EXECUTE;
	  ble_send_cp(ble, buffer, 1);
	  returnCode = ble_wait_run(ble);
	  if (returnCode != BLE_DFU_RESP_VAL_SUCCESS){
	    dfuPrintHumanReadableError(ble);
	    return returnCode;
	  }
	}
	else {
	  //Request command execution to acknowledge the send data
	  ble_wait_setup(ble, OP_CODE_EXECUTE);
	  buffer[0]         = OP_CODE_EXECUTE;
	  ble_send_cp(ble, buffer, 1);
	  returnCode = ble_wait_run(ble);
	  if (returnCode != BLE_DFU_RESP_VAL_SUCCESS){
	    dfuPrintHumanReadableError(ble);
	    return returnCode;
	  }
	  if (transferSize > packageDataLength - returnedOffset) { transferSize = packageDataLength - returnedOffset; }
	  //Create a new object
	  ble_wait_setup(ble, OP_CODE_CREATE);
	  buffer[0]         = OP_CODE_CREATE;
	  buffer[1]         = (uint8_t)packageType;
	  buffer[2]         = (transferSize>> 0) & 0xFF;
	  buffer[3]         = (transferSize>> 8) & 0xFF;
	  buffer[4]         = (transferSize>>16) & 0xFF;
	  buffer[5]         = (transferSize>>24) & 0xFF;  
	  ble_send_cp(ble, buffer, 6);
	  returnCode = ble_wait_run(ble);
	  if (returnCode != BLE_DFU_RESP_VAL_SUCCESS){
	    dfuPrintHumanReadableError(ble);
	    return returnCode;
	  }
	}
      }
      else {
	//CRC error, retry the transmission
	printf("CRC error for %u bytes, will now retransmit the data\n", returnedOffset);
	if (transferSize > packageDataLength - returnedOffset) { transferSize = packageDataLength - returnedOffset; }
	//Create a new object
	ble_wait_setup(ble, OP_CODE_CREATE);
	buffer[0]         = OP_CODE_CREATE;
	buffer[1]         = (uint8_t)packageType;
	buffer[2]         = (transferSize>> 0) & 0xFF;
	buffer[3]         = (transferSize>> 8) & 0xFF;
	buffer[4]         = (transferSize>>16) & 0xFF;
	buffer[5]         = (transferSize>>24) & 0xFF;  
	ble_send_cp(ble, buffer, 6);
	returnCode = ble_wait_run(ble);
	if (returnCode != BLE_DFU_RESP_VAL_SUCCESS){
	  dfuPrintHumanReadableError(ble);
	  return returnCode;
	}
      }
    }
    else {
      printf("Unexpected 0 bytes transferred reply, this should never happen\n");
      return BLE_DFU_RESP_VAL_OPPERATION_FAILED;
    }
  }

  printf("= DONE =\n");
  return BLE_DFU_RESP_VAL_SUCCESS;
}




int dfu (const char *bdaddr, uint8_t * dat,
     size_t dat_sz, uint8_t * bin, size_t bin_sz)
{
  BLE *ble;
  uint8_t retries;
  uint8_t maxRetries=3;
  uint8_t done = 0;
  
  ble_init();
  ble = ble_open(bdaddr);
  if (ble==0){
    fprintf(stderr, "Failed open BLE connection\n");
    return BLE_DFU_RESP_VAL_OPPERATION_FAILED;
  }
  
  for (retries=0; retries<maxRetries && (!done); retries++){

    ble->debug = 0;
    if (ble == 0){
      printf("ERROR: Unable to ble_open with address=%s\n", bdaddr);
      break;
    }

    if (ble_register_notify(ble)){
      printf("ERROR: Unable to ble_register_notify\n");
      break;
    }

    printf("\n\n=== Sending INIT package  ===\n");
    if (dfuSendPackage(ble, dat, dat_sz, BLE_OBJ_TYPE_COMMAND) == BLE_DFU_RESP_VAL_SUCCESS){
      printf("\n\n=== Sending DATA package  ===\n");
      if (dfuSendPackage(ble, bin, bin_sz, BLE_OBJ_TYPE_DATA) == BLE_DFU_RESP_VAL_SUCCESS){
	done = 1;
      }
    }




    if (retries<maxRetries && (!done)) {
      printf("Operation failed, retrying in 3 seconds\n\n\n");
      sleep (3);
    }
  }

  ble_close(ble);
  
  if (retries == maxRetries){
    printf("Too many retries, the operation failed!!!\n");
    return BLE_DFU_RESP_VAL_OPPERATION_FAILED;
  }
  else {
    return BLE_DFU_RESP_VAL_SUCCESS;
  }

}
