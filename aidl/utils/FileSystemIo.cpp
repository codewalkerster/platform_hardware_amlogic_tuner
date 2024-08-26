/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */


#include "FileSystemIo.h"

#ifdef ANDROID

static android::SystemControlClient *mSystemControl = NULL;

size_t FileSystem_create() {
    if (mSystemControl == NULL) {
        mSystemControl = android::SystemControlClient::getInstance();
        return -1;
    }
    return mSystemControl == NULL ? -1 : 0;
}

size_t FileSystem_readFile(const char *name, char *value, size_t len) {
    std::string strvalue;
    if (mSystemControl == NULL || name == NULL || value == NULL)
        return -1;

    mSystemControl->readSysfs(name, strvalue);
    strncpy(value, strvalue.c_str(), len);
    return 0;
}


size_t FileSystem_writeFile(const char *name, const char *value) {
    if (mSystemControl == NULL || name == NULL || value == NULL)
        return -1;

    mSystemControl->writeSysfs(name, value, strlen(value));
    return 0;
}

size_t FileSystem_getPropertyInt(const char *name, int32_t def) {
    if (mSystemControl == NULL || name == NULL)
        return -1;
    return mSystemControl->getPropertyInt(name, def);
}

size_t FileSystem_release() {
    if (mSystemControl == NULL)
        return -1;

    delete mSystemControl;
    mSystemControl = NULL;
    return 0;
}

#else

size_t FileSystem_create()  {
    return 0;
}

size_t FileSystem_readFile(const char *name, char *cmd) {
    int fd, len, ret;
    ALOGV("open file \"%s\"\n", name);

    fd = open(name, O_WRONLY);
    if (fd == -1)
    {
        ALOGV("cannot open file \"%s\"", name);
        return -1;
    }

    len = strlen(cmd);

    ret = read(fd, cmd, len);
    if (ret != len)
    {
        ALOGV("read failed file:\"%s\" cmd:\"%s\" error:\"%s\"\n", name, cmd, strerror(errno));
        close(fd);
        return AM_Dmx_ERROR;
    }
    else
        ALOGV("read file:\"%s\" cmd:\"%s\" ok \n", name, cmd);

    close(fd);
    return AM_Dmx_SUCCESS;
}


size_t FileSystem_writeFile(const char *name, const char *cmd) {
    int fd, len, ret;
    ALOGV("open file \"%s\"\n", name);

    fd = open(name, O_WRONLY);
    if (fd == -1)
    {
        ALOGV(fmt,...)("cannot open file \"%s\"", name);
        return AM_Dmx_ERROR;
    }

    len = strlen(cmd);

    ret = write(fd, cmd, len);
    if (ret != len)
    {
        ALOGV("write failed file:\"%s\" cmd:\"%s\" error:\"%s\"\n", name, cmd, strerror(errno));
        close(fd);
        return AM_Dmx_ERROR;
    }
    else
        ALOGV("write file:\"%s\" cmd:\"%s\" ok \n", name, cmd);
    close(fd);
    return AM_Dmx_SUCCESS;
}


int FileSystem_setAudioParam(int param1, int param2, int param3);
{
    return 0;
}

int FileSystem_release();
{
    return 0;
}


#endif
