#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{   
    if (argc > 3 || argc == 1)
    {
        printf("Wrong number of parameters!");
        return -1;
    }
    else
    {
        // daca am doar un argument, se face criptare
        if (argc == 2)
        {
            // deschid fisierul in care sunt cuvintele
            int fd = open(argv[1], O_RDWR, S_IRUSR | S_IWUSR);
            if (fd == -1)
            {
                perror(NULL);
                return errno;
            }

            struct stat stats;

            // iau marimea fisierului
            if (fstat(fd, &stats) == -1)
            {
                perror(NULL);
                return errno;
            }

            // incarc fisierul in memoria partajata
            char *shmFile = mmap(0, stats.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (shmFile == MAP_FAILED)
            {
                perror(NULL);
                return errno;
            }

            // creez memorie partajata pentru a pune permutarile pe care urmeaza sa le fac
            const char *outputPerm = "perm";
            int permF;
            permF = shm_open(outputPerm, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
            if (permF < 0)
            {
                perror(NULL);
                return errno;
            }

            // trunchez memoria la 1024 bytes
            int perm_size = 1024;
            if (ftruncate(permF, perm_size) == -1)
            {
                perror(NULL);
                shm_unlink(outputPerm);
                return errno;
            }

            // mapez memoria
            char *shmPerm = mmap(0, perm_size, PROT_READ | PROT_WRITE, MAP_SHARED, permF, 0);
            if (shmPerm == MAP_FAILED)
            {
                perror(NULL);
                shm_unlink(outputPerm);
                return errno;
            }

            pid_t pid;
            int delim = -1;
            int nrCuv = 0;
            for (int i = 0; i < stats.st_size; ++i)
            {
                // verific daca am ajuns la finalul unui cuvant
                if (shmFile[i] == ' ' || shmFile[i] == '\n' || i + 1 == stats.st_size)
                {
                    // verific daca am doua caractere delim consecutive
                    if (delim != i - 1)
                    {
                        ++nrCuv;
                        // offset-ul la care scriu in memorie
                        int nrB = (nrCuv - 1) * 256;
                        // verific daca offset-ul este mai mare decat dimensiunea memoriei, daca da, maresc dimensiunea memoriei
                        if (nrB > perm_size)
                        {
                            perm_size = nrB * 2;
                            if (ftruncate(permF, perm_size) == -1)
                            {
                                perror(NULL);
                                shm_unlink(outputPerm);
                                return errno;
                            }

                            shmPerm = mmap(0, perm_size, PROT_READ | PROT_WRITE, MAP_SHARED, permF, 0);
                            if (shmPerm == MAP_FAILED)
                            {
                                perror(NULL);
                                shm_unlink(outputPerm);
                                return errno;
                            }
                        }
                        // creez un proces copil
                        pid = fork();
                        if (pid < 0)
                            return errno;
                        else if (pid == 0)
                        {
                            // in procesul copil aplic permutarea random
                            time_t t;
                            srand((unsigned)time(&t));
                            int x;
                            // in cazul in care caracterul face parte din cuvant, inseamna ca este ultimul caracter al fisierului,
                            // incrementez i cu 1 pentru a considera urmatoarea pozitie ca delim
                            if (shmFile[i] != ' ' && shmFile[i] != '\n')
                                ++i;
                            // x reprezinta lungimea sirului
                            if (delim == -1)
                                x = i;
                            else
                                x = i - delim - 1;

                            //vector in care retin permutarea aplicata
                            int permArray[x];
                            for (int j = 0; j < x; ++j)
                                permArray[j] = j + 1;

                            // aplic o permutare random
                            int q = x;
                            for (int j = i - 1; j > delim; --j)
                            {
                                int y = (rand() % x) + delim + 1;
                                char z = shmFile[j];
                                shmFile[j] = shmFile[y];
                                shmFile[y] = z;

                                int a = permArray[j - delim - 1];
                                permArray[j - delim - 1] = permArray[y - delim - 1];
                                permArray[y - delim - 1] = a;

                                --x;
                            }

                            // scriu la offset-ul corespunzator in memoria partajata permutarea
                            nrB += sprintf(shmPerm + nrB, "%d ", permArray[0]);
                            for (int j = 1; j < q; ++j)
                            {
                                nrB += sprintf(shmPerm + nrB, "%d ", permArray[j]);
                            }

                            nrB += sprintf(shmPerm + nrB, "\n");
                            break;
                        }
                    }
                    delim = i;
                }
            }

            if (pid > 0)
            {
                // astept ca toate procesele sa se termine
                for (int i = 0; i < nrCuv; ++i)
                    wait(NULL);

                // deschid fisierul in care scriu permutarile
                int dest = open("permutari", O_CREAT | O_WRONLY | O_TRUNC | O_APPEND, S_IRWXU);
                if (dest < 0)
                {
                    perror(NULL);
                    return errno;
                }

                int w;
                char prm[256];
                char chr;
                for (int i = 0; i < nrCuv; ++i)
                {
                    // citesc cate o permutare din memoria partajata si o scriu in fisier
                    sscanf(shmPerm + i * 256, "%256[^\n]%c", prm, &chr);
                    strcat(prm, &chr);

                    w = write(dest, prm, strlen(prm));
                    if (w < 0)
                    {
                        perror(NULL);
                        return errno;
                    }
                }
                close(fd);
                close(dest);
            }
        }
        else
        {
            // deschid fisierul in care am cuvintele criptate si fisierul cu permutarile
            FILE *cuv = fopen(argv[1], "r");
            FILE *perm = fopen(argv[2], "r");

            const char *decrName = "decriptari";
            int decrF;
            //creez memoria partajata in care sa pun cuvintele decriptate
            decrF = shm_open(decrName, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
            if (decrF < 0)
            {
                perror(NULL);
                return errno;
            }

            int decrypt_size = 1024;
            if (ftruncate(decrF, decrypt_size) == -1)
            {
                perror(NULL);
                shm_unlink(decrName);
                return errno;
            }

            char *shmDecr = mmap(0, decrypt_size, PROT_READ | PROT_WRITE, MAP_SHARED, decrF, 0);
            if (shmDecr == MAP_FAILED)
            {
                perror(NULL);
                shm_unlink(decrName);
                return errno;
            }

            pid_t pid;
            int nrCuv = 0;
            char wrd[256];
            char strPerm[256];
            // citesc cate un cuvant din fisier
            while (fscanf(cuv, "%s", wrd) != EOF)
            {
                ++nrCuv;
                // offset-ul la care scriu in memorie
                int nrB = (nrCuv - 1) * 256;
                // verific ca offset-ul sa nu fie mai mare
                if (nrB > decrypt_size)
                {
                    decrypt_size = nrB * 2;
                    if (ftruncate(decrF, decrypt_size) == -1)
                    {
                        perror(NULL);
                        shm_unlink(decrName);
                        return errno;
                    }

                    shmDecr = mmap(0, decrypt_size, PROT_READ | PROT_WRITE, MAP_SHARED, decrF, 0);
                    if (shmDecr == MAP_FAILED)
                    {
                        perror(NULL);
                        shm_unlink(decrName);
                        return errno;
                    }
                }
                int x = strlen(wrd);
                char res[x + 1];
                // citesc cate o permutare din fisier
                fgets(strPerm, 256, perm);
                // creez un proces copil
                pid = fork();
                if (pid < 0)
                    return errno;
                else if (pid == 0)
                {
                    // in procesul copil decriptez cuvantul
                    int bytesConsumed = 0;
                    int bytesNow = 0;
                    for (int j = 0; j < x; ++j)
                    {
                        int p;
                        sscanf(strPerm + bytesConsumed, "%d%n", &p, &bytesNow);
                        bytesConsumed += bytesNow;
                        res[p - 1] = wrd[j];
                    }
                    res[x] = '\0';
                    
                    sprintf(shmDecr + nrB, "%s\n", res);
                    fclose(cuv);
                    fclose(perm);
                    return 0;
                }
            }
            if (pid > 0)
            {
                // astept ca toate procesele copil sa se termine
                for (int i = 0; i < nrCuv; ++i)
                    wait(NULL);

                // deschid fisierul in care scriu decriptarile
                int dest = open("decriptari", O_CREAT | O_WRONLY | O_TRUNC | O_APPEND, S_IRWXU);
                if (dest < 0)
                {
                    perror(NULL);
                    return errno;
                }

                int w;
                char chr;
                // scriu decriptarile in fisier
                for (int i = 0; i < nrCuv; ++i)
                {
                    sscanf(shmDecr + i * 256, "%256[^\n]%c", wrd, &chr);
                    strcat(wrd, &chr);

                    w = write(dest, wrd, strlen(wrd));
                    if (w < 0)
                    {
                        perror(NULL);
                        return errno;
                    }
                }
                
                // eliberez memoria si inchid fisierele
                if (munmap(shmDecr, decrypt_size) == -1)
                {
                    perror(NULL);
                    return errno;
                }

                if (shm_unlink(decrName) == -1)
                {
                    perror(NULL);
                    return errno;
                }

                fclose(cuv);
                fclose(perm);
            }
        }
    }

    return 0;
}