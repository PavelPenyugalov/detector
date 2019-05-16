/**
 * Copyright (C) 2018 ARSPRO
 *
 * Author: Pavel Penyugalov <pavel.penyugalov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
**/

/**
*	EXIT CODE VALUES :
*		0 - exit normally
*		-1 - wrong parameters number
*		-2 - wrong input parameters or files expansion
*		-3 - error opening file for read
*		-4 - error allocating memory
*		-5 - read error
**/

#include <stdio.h>
#include <conio.h>
#include <iostream>
#include <algorithm>
#include <fstream>

#include "x265.h"
#include "x265_config.h"
#include "metadataFromJson.h"

#define MAX 255
#define BYTELEN 64
#define HEADERLEN 6

//TODO
// добавить дополнительные алгоритмы проверки в соответствии с указанным пользователем режимом

enum MODE { ONE = 0, TWO, THREE, FOUR };
const uint8_t headerMetadata[HEADERLEN] = { 181, 0, 60, 0, 1, 4 };
std::vector<uint8_t> injectedData;
std::vector<uint8_t> generatedData;

bool compareValues(uint8_t v1, uint8_t v2) { return v1 > v2; }

void parseCMDarguments(char*, string&);
bool checkExpansion(char*, char*);
std::ifstream::pos_type getFileSize(const char*);
size_t choosePartFileSize(size_t);
bool checkHeaderMetadata(const uint8_t*, uint8_t*, size_t, size_t&);
int writeInjectedMetadata(std::vector<uint8_t>*, uint8_t*, size_t, size_t);
int writeGeneratedMetadata(std::vector<uint8_t>*, uint8_t**, size_t, int);
double compare2vectors(std::vector<uint8_t>, std::vector<uint8_t>);
int countInjectedFrameMovie(std::vector<uint8_t>, uint8_t*, size_t);

int main(int argc, char** argv)
{
	char* videoFilePath = NULL;
	char* jsonFilePath = NULL;
	FILE* videoFile = NULL;
	FILE* jsonFile = NULL;
	uint8_t** ptr_cim = NULL;
	uint8_t* ptr_cim_frame = NULL;
	uint8_t* videoByteBuffer = NULL;
	string  argValue = "";
	errno_t err;
	size_t videoInByteSize = 0;
	size_t readingResult = 0;
	size_t byteForRead = 0;
	size_t lastCounterValue = 0;	// крайнее значение счетчика на выходе из функции checkHeaderMetadata()
	bool videoFlag = false;
	bool jsonFlag = false;
	bool expansionFlag = false;
	bool headerCoincidenceFlag = false;
	int cim_info = 0;
	int frameByteCim = 0;
	int injectedBytes = 0;
	int geneeratedBytes = 0;
	int mode = ONE;
	int frameCounter = 0;
	double vectorCompareResult = 0.0;

	std::ifstream::pos_type fileSize;

	if (argc < 5) {
		printf("%s\n", "You need to point at least 2 arguments!");
		printf("%s\n", "****");
		printf("%s\n", "Example of usage: -i inputVideo.mp4 -j inputJson.json");
		exit(-1);
	}

	for (int i = 0; i < argc; i++) {
		parseCMDarguments(argv[i], argValue);
		if ((argValue == "-i") && (argc > i)) {
			// проверка расширения .mp4
			videoFilePath = argv[i + 1];
			videoFlag = true;
			expansionFlag = checkExpansion(videoFilePath, ".mp4");
			if (expansionFlag == false) {
				printf("%s\n", "Wrong format expansion! You need to use only .mp4 files!");
				exit(-2);
			}
		}
		else if ((argValue == "-j") && (argc > i)) {
			// проверка расширения .json
			jsonFilePath = argv[i + 1];
			jsonFlag = true;
			expansionFlag = checkExpansion(jsonFilePath, ".json");
			if (expansionFlag == false) {
				printf("%s\n", "Wrong format expansion! You need to use only .json metadata files!");
				exit(-2);
			}
		}
	}

	if (videoFlag != true || jsonFlag != true) {
		if (videoFlag == false) {
			printf("%s\n", "No flag -i in the input options!");
			exit(-2);
		}
		if (jsonFlag == false) {
			printf("%s\n", "No flag -j in the input options!");
			exit(-2);
		}
	}

	err = fopen_s(&videoFile, videoFilePath, "rb");

	if ( err != 0 ) {
		printf("Cant open %s for read!\n", videoFilePath);
		exit(-3);
	}

	videoInByteSize = static_cast<size_t>(getFileSize(videoFilePath));
	
	//fseek(videoFile, 0, SEEK_END);
	//videoInByteSize = ftell(videoFile);
	
	if (videoInByteSize != 0) {
		std::cout << "Input video file contains " << videoInByteSize << " bytes" << std::endl;
		byteForRead = choosePartFileSize(videoInByteSize);		// кол-во байтов от начала файла для поиска заголовков метаданных
	}

	err = fopen_s(&jsonFile, jsonFilePath, "r");

	if ( err != 0 ) {
		printf("Cant open %s for read!\n", jsonFilePath);
		exit(-3);
	}

	metadataFromJson meta;
	cim_info = meta.movieMetadataFromJson(jsonFilePath, ptr_cim);
	frameByteCim = meta.mByteCounter;
	if (cim_info != 0) {
		printf("The JSON file contains information about %d scene frames\n", cim_info);
		ptr_cim_frame = ptr_cim[0];
	}
	
	printf("%s\n", "Begin search metadata headers in input video file...");
	fseek(videoFile, 0, SEEK_SET);

	if (mode == ONE) {
		videoByteBuffer = (uint8_t*)malloc(sizeof(uint8_t) * byteForRead);
		if (!videoByteBuffer) {
			printf("%s\n", "Can't allocate memory for buffer! Please check available memory space");
			exit(-4);
		}
	}

	readingResult = fread(videoByteBuffer, 1, byteForRead, videoFile);
	if (readingResult != byteForRead) {
		printf("%s\n", "Read error!");
		exit(-5);
	}

	headerCoincidenceFlag = checkHeaderMetadata(headerMetadata, videoByteBuffer, byteForRead, lastCounterValue);

	if (headerCoincidenceFlag == false) {
		printf("\n%s\n", "Not found metadata headers in part of file!");
		printf("%s\n", "This is mean what input movie file doesn't contain HDR10plus metadata at all or only partially");
		exit(0);
	}
	else {
		printf("\n%s\n", "Info: found headers HDR10plus metadata (country code, terminal provider code, provider oriented code)");
	}

	// если заголовки найдены подготавливаем массивы для хранения и поиска данных
	fseek(videoFile, 0, SEEK_SET);
	free(videoByteBuffer);

	videoByteBuffer = (uint8_t*)malloc(sizeof(uint8_t) * videoInByteSize);
	if (!videoByteBuffer) {
		printf("%s\n", "Can't allocate memory for buffer! Please check available memory space");
		exit(-4);
	}

	readingResult = fread(videoByteBuffer, 1, videoInByteSize, videoFile);
	if (readingResult != videoInByteSize) {
		printf("%s\n", "Read error!");
		exit(-5);
	}

	injectedBytes = writeInjectedMetadata(&injectedData, videoByteBuffer, lastCounterValue, frameByteCim);
	if (injectedBytes != frameByteCim) {
		printf("%s\n", "Warning: the number of extracted metadata does not match the number of generated metadata!");
	}

	geneeratedBytes = writeGeneratedMetadata(&generatedData, ptr_cim, frameByteCim, 0);
	if (geneeratedBytes != frameByteCim) {
		printf("%s\n", "Warning: the number of extracted metadata does not match the number of generated metadata!");
	}

	//TODO
	// алгоритм сравнения векторов
	vectorCompareResult = compare2vectors(injectedData, generatedData);

	if (vectorCompareResult != 100) {
		printf("\nWarning: injected and generated metadata matched only on %.2f percents\n", vectorCompareResult);
	}
	else {
		printf("\n%s\n", "Info: injected and generated metadata matched");
	}

	// далее просчитываем количество кадров с вложенными метаданными
	printf("\n%s\n", "Start count injected frames in input video file...");
	frameCounter = countInjectedFrameMovie(injectedData, videoByteBuffer, videoInByteSize);

	printf("\n%s\n", "Count completed!");

	if (frameCounter != cim_info) {
		printf("Warning: Found only %d injected metadata frames from %d\n", frameCounter, cim_info);
		printf("%s\n", "This might mean what you're using not appropriate metadata file");
	}
	else {
		printf("Info: Found all %d injected metadata frames\n", frameCounter);
	}

	free(videoByteBuffer);
	fclose(videoFile);
	fclose(jsonFile);

	printf("%s\n", "Done!");

	return 0;
}

void parseCMDarguments(char* argument, string& buffer)
{
	if (argument) {
		if (buffer.size() > 0) {
			buffer.clear();
		}
		char* argPtr = argument;
		size_t argLen = std::strlen(argPtr);
		int i = 0;
		while ( i != argLen ) {
			buffer.push_back(*argPtr++);
			i++;
		}
	}
}
bool checkExpansion(char* filePath, char* exp)
{
	char* ptrFilePath = filePath;
	char* expansion = std::strrchr(ptrFilePath, '.');
	bool flag = true;
	if (strcmp(expansion, exp) != 0) {
		flag = false;
	}

	return flag;
}

std::ifstream::pos_type getFileSize(const char* filename)
{
	std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
	return in.tellg();
}

size_t choosePartFileSize(size_t fSize)
{
	size_t results = 0;
	if (fSize < 1000000000) results = fSize / 10;
	else if (fSize < 10000000000) results = fSize / 10000;
	else if (fSize < 100000000000) results = fSize / 100000;
	else if (fSize < 1000000000000) results = fSize / 1000000;
	else results = fSize;

	return results;
}

bool checkHeaderMetadata(const uint8_t* sourceArray, uint8_t* movieStream, size_t lenBytes, size_t& fCounterValue) 
{
	size_t counter = 0;
	int arrayIndex = 0;
	bool flag = false;
	uint8_t* tempMovieByteExt;
	uint8_t* tempMovieByteIn;

	tempMovieByteExt = movieStream;
	while (counter != lenBytes) {
		if (*tempMovieByteExt == sourceArray[arrayIndex]) {
			int inlineCounter = 1;
			arrayIndex = 0;
			tempMovieByteIn = tempMovieByteExt;
			while (inlineCounter != HEADERLEN) {
				tempMovieByteIn++;
				if (*tempMovieByteIn == sourceArray[inlineCounter]) {
					flag = true;
					inlineCounter++;
					continue;
				}
				flag = false;
				inlineCounter++;
				break;
			}
		}
		if (flag == true) {
			fCounterValue = counter;
			break;
		}
		counter++;
		tempMovieByteExt++;
	}

	return flag;
}

int writeInjectedMetadata(std::vector<uint8_t>* injV, uint8_t* movieStream, size_t offset, size_t amountBytes)
{
	int counter = 0;
	uint8_t* tempMovieByte = movieStream + offset;
	while (counter != amountBytes) {
		injV->push_back(*tempMovieByte);
		tempMovieByte++;
		counter++;
	}

	return counter;
}

int writeGeneratedMetadata(std::vector<uint8_t>* genV, uint8_t** ptrCim, size_t amountBytes, int index)
{
	int counter = 0;
	uint8_t* tempArrayByte = ptrCim[index];
	if (*tempArrayByte != 0xB5) {
		tempArrayByte++;
	}

	// возможен выход за диапазон, если не соответствуе сгенерированный .json и .mp4
	while (counter != amountBytes) {
		genV->push_back(*tempArrayByte);
		tempArrayByte++;
		counter++;
	}
	return counter;
}

double compare2vectors(std::vector<uint8_t> injV, std::vector<uint8_t> genV)
{
	size_t counter = 0;
	double result = 0.0;
	double difference = 0.0;
	bool flag = false;

	std::sort(injV.begin(), injV.end(), compareValues);
	std::sort(genV.begin(), genV.end(), compareValues);

	for (int i = 0; i < injV.size(); i++) {
		if (genV.size() > i) {
			for (int j = 0; j < genV.size(); j++) {
				if (injV[i] == genV[j]) {
					flag = true;
					counter++;
					break;
				}
			}
		}
	}

	counter = injV.size() - counter;
	if (counter == 0)
		result = 100.0;
	else {
		difference = (counter * 100) / (double)injV.size();
		result = 100 - difference;
	}

	return result;
}

int countInjectedFrameMovie(std::vector<uint8_t> sourceVector, uint8_t* movieStream, size_t lenBytes)
{
	size_t counter = 0;
	int arrayIndex = 0;
	int oldValue = 0;
	bool flag = false;
	uint8_t* tempMovieByteExt;
	uint8_t* tempMovieByteIn;
	int frameCounter = 0;

	tempMovieByteExt = movieStream;
	while (counter != lenBytes) {
		if (*tempMovieByteExt == sourceVector.at(arrayIndex)) {
			int inlineCounter = 0;
			arrayIndex = 0;
			tempMovieByteIn = tempMovieByteExt;
			while (inlineCounter != sourceVector.size()) {
				if (*tempMovieByteIn == sourceVector.at(inlineCounter)) {
					flag = true;
					inlineCounter++;
					oldValue = inlineCounter;
					tempMovieByteIn++;
					continue;
				}
				
				flag = false;
				inlineCounter++;
				break;
			}
		}
		if ((flag == true) && (oldValue == sourceVector.size())) {
			oldValue = 0;
			flag = false;
			frameCounter++;
		}
		else if ((flag == false) && (oldValue > HEADERLEN)) {
			oldValue = 0;
			int i = 0;
			size_t vectorSize = sourceVector.size();
			uint8_t* tempPtr = tempMovieByteExt;
			sourceVector.clear();
			while (i != vectorSize) {
				sourceVector.push_back(*tempPtr);
				tempPtr++;
				i++;
			}
			frameCounter++;
		}

		counter++;
		tempMovieByteExt++;
	}

	return frameCounter;
}