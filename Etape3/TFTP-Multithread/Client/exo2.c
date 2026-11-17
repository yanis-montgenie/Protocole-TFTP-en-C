#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define N 1024
double A[N][N];
double B[N][N];
double C[N][N];

int m; //nombre de slice

void *mult_slice(void *arg) {
	int d,i,j,k,s=*((int *) arg);
	d=s*N/m;
	printf("(%d) calcul des lignes %d à %d\n",s,d,d+N/m-1);
	for (i=d;i<d+N/m;i++)
		for (j=0;j<N;j++) {
			C[i][j]=0.0;
			for (k=0;k<N;k++) C[i][j]+=A[i][k]*B[k][j];
	}
	printf("(%d) fin calcul des lignes %d à %d\n",s,d,d+N/m-1);
}

int main(int argc,char *argv[]) {
	int i,j;
	pthread_t t[16];
	int arg[16];

	if (argc!=2) {
		fprintf(stderr,"usage %s n, ou n=1,2,4,8 ou 16\n",*argv);
		exit(2);
	}

	m=atoi(argv[1]);

	switch (m) {
		case 1: break;
		case 2: break;
		case 4: break;
		case 8: break;
		case 16: break;
		default: fprintf(stderr,"valeur %d incorrecte\n",m); exit(2);
	}

	for (i=0;i<N;i++)
		for (j=0;j<N;j++) {
			A[i][j]=drand48();
			B[i][j]=drand48();
	}
	
	printf("matrice initialisées\n");
	
	for (i=0;i<m;i++) {
		arg[i]=i;
		pthread_create(t+i,NULL,mult_slice,arg+i);
	}
	printf("calcul en cours\n");
	
	for (i=0;i<m;i++) pthread_join(t[i],NULL);
	
	printf("calcul terminé\n");
}
