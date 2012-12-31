/*
 * Copyright (C) 2012 Tomasz Rostanski
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "CameraConfig"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h> 

#include <cutils/log.h>

#define CONFIG_FILENAME "/data/misc/camera/config.txt"
#define PREVIEW_MODE    "preview_mode"
#define ROTATION_MODE   "rotation_mode"

typedef struct
{
    int preview;
    int rotation;
} config_t;

void read_config(config_t *config)
{
    FILE *fp = fopen(CONFIG_FILENAME, "r");
    if (fp)
    {
        char buff[80], *tmp;
        while (fgets(buff, sizeof(buff), fp) != NULL)
        {
            if ((tmp = strstr(buff, PREVIEW_MODE)) != NULL)
            {
                tmp += sizeof(PREVIEW_MODE);
				config->preview = atoi(tmp);    
	    	}
            else if ((tmp = strstr(buff, ROTATION_MODE)) != NULL)
            {
                tmp += sizeof(ROTATION_MODE);
				config->rotation = atoi(tmp);    
	    	}	
        }
        fclose(fp);
    }
    else
        ALOGI("Unable to open %s\n", CONFIG_FILENAME);
}

int write_config(config_t *config)
{
    FILE *fp = fopen(CONFIG_FILENAME, "w");
    if (fp)
    {
        fprintf(fp, "%s=%d\n", PREVIEW_MODE, config->preview);
        fprintf(fp, "%s=%d\n", ROTATION_MODE, config->rotation);
        fclose(fp);
        return 0;
    }
    ALOGI("Unable to save %s\n", CONFIG_FILENAME);
	return -1;
}

void usage(char *name)
{
    printf("Usage: %s get [preview/rotation]\n", name);
    printf("       %s set [preview/rotation] [value]\n", name);
    printf("       preview values: 0 - front cam mirrored, 1 - front cam normal, 2 - rear cam\n");
    printf("       rotation values: 0, 90, 180, 270\n");
}

int main(int argc, char **argv)
{
    config_t config;
    memset(&config, 0, sizeof(config));
    read_config(&config);

    if (argc == 3 && strcmp(argv[1], "get") == 0)
    {
		if (strcmp(argv[2], "preview") == 0)
            printf("%d", config.preview);
		else if (strcmp(argv[2], "rotation") == 0)
            printf("%d", config.rotation);
        return 0;
    }
    else if (argc == 4 && strcmp(argv[1], "set") == 0)
    {
		if (strcmp(argv[2], "preview") == 0)
            config.preview = atoi(argv[3]);
		else if (strcmp(argv[2], "rotation") == 0)
            config.rotation = atoi(argv[3]);
        return write_config(&config);
    }

    usage(argv[0]);
    return -1;
}
