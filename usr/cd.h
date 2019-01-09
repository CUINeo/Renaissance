#ifndef CD_H
#define CD_H

int str_equal(char *dir, char *cmp, int len);

int str_len(char *str);

int cd(char *para, char *pwd);

int combine(char *dest, char *str1, char *str2, char append);

#endif