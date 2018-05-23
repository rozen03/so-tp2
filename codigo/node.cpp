#include "node.h"
#include "picosha2.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <cstdlib>
#include <queue>
#include <atomic>
#include <mpi.h>
#include <map>
#include <unistd.h>
int total_nodes, mpi_rank;
Block *last_block_in_chain;
map<string,Block> node_blocks;
atomic<bool> probando;
vector<int> casos(6,0);
/*
struct Block {
  unsigned int index;
  unsigned int node_owner_number;
  unsigned int difficulty;
  unsigned long int created_at;
  char nonce[NONCE_SIZE];
  char previous_block_hash[HASH_SIZE];
  char block_hash[HASH_SIZE];
};
*/
//Cuando me llega una cadena adelantada, y tengo que pedir los nodos que me faltan
//Si nos separan más de VALIDATION_BLOCKS bloques de distancia entre las cadenas, se descarta por seguridad
void printCasos(){
	printf("[%d] Casos 0:%d 1:%d 2:%d 3:%d 4:%d 5:%d 6:%d \n",mpi_rank,casos[0],casos[1],casos[2],casos[3],casos[4],casos[5],casos[6]);
}
bool verificar_y_migrar_cadena(const Block *rBlock, const MPI_Status *status){
	printf("[%d] Entre bien", mpi_rank);
	//TODO: Enviar mensaje TAG_CHAIN_HASH
	MPI_Send(rBlock, 1, *MPI_BLOCK, status->MPI_SOURCE, TAG_CHAIN_HASH, MPI_COMM_WORLD);
	Block *blockchain = new Block[VALIDATION_BLOCKS];

	//TODO: Recibir mensaje TAG_CHAIN_RESPONSE
	MPI_Status sttatus;
	MPI_Recv(blockchain, VALIDATION_BLOCKS, *MPI_BLOCK, status->MPI_SOURCE, TAG_CHAIN_RESPONSE, MPI_COMM_WORLD, &sttatus);
	int count;
	MPI_Get_count(&sttatus,*MPI_BLOCK, &count);
	uint countt = (uint) count;
	//TODO: Verificar que los bloques recibidos
	//sean válidos y se puedan acoplar a la cadena
	//El primer bloque de la lista contiene el hash pedido y el mismo index que el bloque original.
	if(blockchain[0].index != rBlock->index || strcmp(blockchain[0].block_hash,rBlock->block_hash)!= 0){
		printf("[%d] %d me dio basura \n", mpi_rank, status->MPI_SOURCE);
		cout<<"LO INICE FUERON: "<<blockchain[0].index <<" " <<rBlock->index;
		delete []blockchain;
		return false;
	}
	// El hash del bloque recibido es igual al calculado por la función block_to_hash.
	string hash;
	block_to_hash(&blockchain[0],hash);
	if(strcmp(hash.c_str(),blockchain[0].block_hash) !=0){
		delete []blockchain;
		return false;
	}

	for (size_t i = 0; i < countt-1; i++) {
		//Cada bloque siguiente de la lista, contiene el hash definido en previous_block_hash del
		//actual elemento.
		if(strcmp(blockchain[i].previous_block_hash,blockchain[i+1].block_hash) != 0){
			delete []blockchain;
		 	return false;
		}
		//Cada bloque siguiente de la lista, contiene el índice anterior al actual elemento.
		if((blockchain[i].index != blockchain[i+1].index+1)){
			delete []blockchain;
			return false;
		}
	}

	bool hayAlguno=false;
	for (size_t i = 0; i < countt; i++) {
		if (node_blocks.find(blockchain[i].block_hash) != node_blocks.end()){
			hayAlguno=true;
			break;
		}
	}
	if(!hayAlguno && blockchain[countt-1].index!=1){
		delete []blockchain;
		return false;
	}
	for (size_t i = 0; i < countt; i++) {
		if (valid_new_block(&blockchain[i])){
			// cout<<mpi_rank<<": CHE ME LLEGO UN bLOQUE DE NUMERO "<<blockchain[i].index<<endl;
			node_blocks[string(blockchain[i].block_hash)]=blockchain[i];
		}else{
			delete []blockchain;
			return false;
		}
	}
	last_block_in_chain = &blockchain[0];
	// cout<<"ahora mi index es"<<last_block_in_chain->index<<endl;
	// delete []blockchain; no descomentar esto, si borramos el array se queda basura en los bloques
		printf("[%d] Sali bien", mpi_rank);
	return true;


 // 	delete []blockchain;
 // 	return false;

}

//Verifica que el bloque tenga que ser incluido en la cadena, y lo agrega si corresponde
bool validate_block_for_chain(const Block *rBlock, const MPI_Status *status){
	// printCasos();
  if(valid_new_block(rBlock)){
	//Agrego el bloque al diccionario, aunque no
	//necesariamente eso lo agrega a la cadena
	node_blocks[string(rBlock->block_hash)]=*rBlock;

	//TODO: Si el índice del bloque recibido es 1
	//y mí último bloque actual tiene índice 0,
	//caso 0
	if(rBlock->index == 1 && last_block_in_chain->index==0){
		//entonces lo agrego como nuevo último.
		last_block_in_chain=(Block *)rBlock;
		casos[0]=1;
		printf("[%d] Agregado a la lista bloque con index %d enviado por %d \n", mpi_rank, rBlock->index,status->MPI_SOURCE);
		return true;
	}

	//TODO: Si el índice del bloque recibido es el siguiente a mí último bloque actual,
	if(rBlock->index == (last_block_in_chain->index + 1)){
		//y el bloque anterior apuntado por el recibido es mí último actual,
		//caso 1
		if(strcmp(rBlock->previous_block_hash,last_block_in_chain->block_hash)==0){
			//entonces lo agrego como nuevo último.
			last_block_in_chain=(Block *)rBlock;
			casos[1]=1;
			printf("[%d] Agregado a la lista bloque con index %d enviado por %d \n", mpi_rank, rBlock->index,status->MPI_SOURCE);
			return true;
			//caso 2
		}else{
			//pero el bloque anterior apuntado por el recibido no es mí último actual,
			//entonces hay una blockchain más larga que la mía.
			printf("[%d] Perdí la carrera por uno (%d) contra %d \n", mpi_rank, rBlock->index, status->MPI_SOURCE);
			bool res = verificar_y_migrar_cadena(rBlock,status);
			casos[2]=1;
			return res;
		}
	}
	//caso 3
	//TODO: Si el índice del bloque recibido es igua al índice de mi último bloque actual,
	if(rBlock->index == last_block_in_chain->index){
	//entonces hay dos posibles forks de la blockchain pero mantengo la mía
		casos[3]=1;
		printf("[%d] Conflicto suave: Conflicto de branch (%u) contra %u \n",mpi_rank,rBlock->index,status->MPI_SOURCE);
		return false;
	}

	//caso 4
	//TODO: Si el índice del bloque recibido es anterior al índice de mi último bloque actual,
	if(rBlock->index < last_block_in_chain->index){
		casos[4]=1;
	//entonces lo descarto porque asumo que mi cadena es la que está quedando preservada.
		printf("[%d] Conflicto suave: Descarto el bloque (%d vs %d) contra %d \n",mpi_rank,rBlock->index,last_block_in_chain->index, status->MPI_SOURCE);
		return false;
	}
	//caso 5
	//TODO: Si el índice del bloque recibido está más de una posición adelantada a mi último bloque actual,
	if(rBlock->index > last_block_in_chain->index){
	//entonces me conviene abandonar mi blockchain actual
		printf("[%d] Perdí la carrera por varios contra %d \n", mpi_rank, status->MPI_SOURCE);
		bool res = verificar_y_migrar_cadena(rBlock,status);
		casos[5]=1;
		return res;
	}
  }
	//caso 6
	casos[6]=1;
  printf("[%d] Error duro: Descarto el bloque recibido de %d porque no es válido \n",mpi_rank,status->MPI_SOURCE);
  return false;
}


//Envia el bloque minado a todos los nodos
void broadcast_block(const Block *block){
  	//No enviar a mí mismo
	//mando desde el de la derecha en adelante, supongo q es un orden distinto... no?
	//copio el bloque por que ya ni se
	Block bloque = *block;
	int dest;
	for (int i = mpi_rank+1; i <total_nodes+mpi_rank ; ++i) {
		dest=i%total_nodes;
		// printf("[%d] mando bloque a: [%d]\n",mpi_rank,dest);
		MPI_Send(&bloque, 1, *MPI_BLOCK, dest, TAG_NEW_BLOCK, MPI_COMM_WORLD);
		// printf("[%d] mande bloque a: [%d]\n",mpi_rank,dest);
	}
}

//Proof of work
//TODO: Advertencia: puede tener condiciones de carrera
void* proof_of_work(void *ptr){
	string hash_hex_str;
	Block block;
	unsigned int mined_blocks = 0;
	auto pid =getpid();
	while(true){
		bool expected = false;
		while (!probando.compare_exchange_weak(expected, true)){
			// printf("soy un boludo esperando");
			expected = false;
		}
		// cout<<"no espere nada lalalala"<<endl;
		block = *last_block_in_chain;

		//Preparar nuevo bloque
		// auto preindex = block.index;
		block.index += 1;
		block.node_owner_number = mpi_rank;
		block.difficulty = DEFAULT_DIFFICULTY;
		memcpy(block.previous_block_hash,block.block_hash,HASH_SIZE);

		//Agregar un nonce al azar al bloque para intentar resolver el problema
		gen_random_nonce(block.nonce);

		//Hashear el contenido (con el nuevo nonce)
		block_to_hash(&block,hash_hex_str);
		// if(probando){
			// continue;
		// }
		//Contar la cantidad de ceros iniciales (con el nuevo nonce)
		if(solves_problem(hash_hex_str)){

			//Verifico que no haya cambiado mientras calculaba
			if(last_block_in_chain->index < block.index){
				mined_blocks += 1;
				*last_block_in_chain = block;
				strcpy(last_block_in_chain->block_hash, hash_hex_str.c_str());
				last_block_in_chain->created_at = static_cast<unsigned long int> (time(NULL));
				node_blocks[hash_hex_str] = *last_block_in_chain;
				printf("[%d][%d] Agregué un producido con index %d\n",mpi_rank,pid,last_block_in_chain->index);

				//TODO: Mientras comunico, no responder mensajes de nuevos nodos
				broadcast_block(last_block_in_chain);
				// printf("[%d][%d]  Se los mande a todos genialmente \n",mpi_rank,pid);
			}
		}
		probando=false;
	}

	return NULL;
}

void mandar_cadena(const Block *rBlock, const MPI_Status *status){
	// last_block_in_chain->
	uint size= min(VALIDATION_BLOCKS,(int)rBlock->index);
	Block *blockchain = new Block[size];
	Block * block=(Block *)rBlock;
	blockchain[0]=*block;
	for (size_t i = 1; i < size; i++) {
		blockchain[i]= node_blocks[block->previous_block_hash];
		block =&node_blocks[block->previous_block_hash];
	}
	MPI_Send(blockchain, size, *MPI_BLOCK, status->MPI_SOURCE, TAG_CHAIN_RESPONSE, MPI_COMM_WORLD);
	// cout<<"MANDE LA CAENA"<<endl;
	//mandar los bloques en orden inverso asi despues "alice" puede fijarse en orden si lesirve
}

int node(){
	probando=false;

	//Tomar valor de mpi_rank y de nodos totales
	MPI_Comm_size(MPI_COMM_WORLD, &total_nodes);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

	//La semilla de las funciones aleatorias depende del mpi_ranking
	srand(time(NULL) + mpi_rank);
	printf("[MPI] Lanzando proceso %u\n", mpi_rank);

 	last_block_in_chain = new Block;

	//Inicializo el primer bloque
	last_block_in_chain->index = 0;
	last_block_in_chain->node_owner_number = mpi_rank;
	last_block_in_chain->difficulty = DEFAULT_DIFFICULTY;
	last_block_in_chain->created_at = static_cast<unsigned long int> (time(NULL));
	memset(last_block_in_chain->previous_block_hash,0,HASH_SIZE);
	//TODO: Crear thread para minar
	pthread_t thread;
	pthread_create(&thread, NULL, proof_of_work, NULL);
	auto pid =getpid();
	while(true){
		Block *  block = new Block;
		//TODO: Recibir mensajes de otros nodos

		MPI_Status status;
		MPI_Recv(block, 1, *MPI_BLOCK,  MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		//   cout<<"Recibí "<<block.node_owner_number<<endl;
		// cout<<"Recibí "<<status.MPI_SOURCE<<" "<<status.MPI_TAG<<endl;
		auto tag= status.MPI_TAG;
		// printf("[%d][%d]  me llego un bloqueh de [%d] con tag [%d]  \n",mpi_rank,pid,status.MPI_SOURCE,tag);

		if (tag ==TAG_NEW_BLOCK){
			//TODO: Si es un mensaje de nuevo bloque, llamar a la función
			// validate_block_for_chain con el bloque recibido y el estado de MPI
			// cout<<"TENGO UN NIU BLOQ"<<endl;
			bool expected = false;
			while (!probando.compare_exchange_weak(expected, true)){
				// printf("[%d][%d]soy una boluda esperando\n",mpi_rank,pid);
				 expected = false;
			}
			// printf("[%d]  me llego un bloqueh \n",mpi_rank);

			validate_block_for_chain(block, &status);
			probando=false;
		}else if(tag==TAG_CHAIN_HASH){
			//TODO: Si es un mensaje de pedido de cadena,
			//responderlo enviando los bloques correspondientes
			// cout<<"LE MANDO LA CAENA"<<endl;
			mandar_cadena(block, &status);
		}
	}

  delete last_block_in_chain;
  return 0;
}
