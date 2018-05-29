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
#include <mutex>
int total_nodes, mpi_rank;
Block *last_block_in_chain;
map<string,Block> node_blocks;
atomic<bool> probando;
//Cuando me llega una cadena adelantada, y tengo que pedir los nodos que me faltan
//Si nos separan más de VALIDATION_BLOCKS bloques de distancia entre las cadenas, se descarta por seguridad

bool verificar_y_migrar_cadena(const Block *rBlock, const MPI_Status *status){
    //TODO: Enviar mensaje TAG_CHAIN_HASH
    MPI_Send(rBlock->block_hash,HASH_SIZE, MPI_CHAR, status->MPI_SOURCE, TAG_CHAIN_HASH, MPI_COMM_WORLD);
    Block *blockchain = new Block[VALIDATION_BLOCKS];

    //TODO: Recibir mensaje TAG_CHAIN_RESPONSE
    MPI_Status sttatus;
    MPI_Recv(blockchain, VALIDATION_BLOCKS, *MPI_BLOCK, status->MPI_SOURCE, TAG_CHAIN_RESPONSE, MPI_COMM_WORLD, &sttatus);
    printf("[%d] %d me dio cositas \n", mpi_rank, status->MPI_SOURCE);
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
    // El hash de los bloques recibidos es igual a los calculado por la función block_to_hash y resuelven el problema.
    for (size_t i = 0; i < countt; i++) {
        string hash_hex_str;
        block_to_hash(&blockchain[i],hash_hex_str);
        if (!((hash_hex_str.compare(blockchain[i].block_hash) == 0) && solves_problem(hash_hex_str))){
            delete []blockchain;
            return false;
        }
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
        node_blocks[string(blockchain[i].block_hash)]=blockchain[i];
    }
    *last_block_in_chain = blockchain[0];
    delete []blockchain;
    return true;

}

//Verifica que el bloque tenga que ser incluido en la cadena, y lo agrega si corresponde
bool validate_block_for_chain(const Block *rBlock, const MPI_Status *status){
    if(valid_new_block(rBlock)){
        //Agrego el bloque al diccionario, aunque no
        //necesariamente eso lo agrega a la cadena
        node_blocks[string(rBlock->block_hash)]=*rBlock;

        //TODO: Si el índice del bloque recibido es 1
        //y mí último bloque actual tiene índice 0,
        //caso 0
        if(rBlock->index == 1 && last_block_in_chain->index==0){
            //entonces lo agrego como nuevo último.
            *last_block_in_chain = *rBlock;
            printf("[%d] Agregado a la lista bloque con index %d enviado por %d \n", mpi_rank, rBlock->index,status->MPI_SOURCE);
            return true;
        }

        //TODO: Si el índice del bloque recibido es el siguiente a mí último bloque actual,
        if(rBlock->index == (last_block_in_chain->index + 1)){
            //y el bloque anterior apuntado por el recibido es mí último actual,
            //caso 1
            if(strcmp(rBlock->previous_block_hash,last_block_in_chain->block_hash)==0){
                //entonces lo agrego como nuevo último.
                *last_block_in_chain = *rBlock;
                printf("[%d] Agregado a la lista bloque con index %d enviado por %d \n", mpi_rank, rBlock->index,status->MPI_SOURCE);
                return true;
                //caso 2
            }else{
                //pero el bloque anterior apuntado por el recibido no es mí último actual,
                //entonces hay una blockchain más larga que la mía.
                printf("[%d] Perdí la carrera por uno (%d) contra %d \n", mpi_rank, rBlock->index, status->MPI_SOURCE);
                bool res = verificar_y_migrar_cadena(rBlock,status);
                return res;
            }
        }
        //caso 3
        //TODO: Si el índice del bloque recibido es igua al índice de mi último bloque actual,
        if(rBlock->index == last_block_in_chain->index){
            //entonces hay dos posibles forks de la blockchain pero mantengo la mía
            printf("[%d] Conflicto suave: Conflicto de branch (%u) contra %u \n",mpi_rank,rBlock->index,status->MPI_SOURCE);
            return false;
        }

        //caso 4
        //TODO: Si el índice del bloque recibido es anterior al índice de mi último bloque actual,
        if(rBlock->index < last_block_in_chain->index){
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
            return res;
        }
    }
    //caso 6
    printf("[%d] Error duro: Descarto el bloque recibido de %d porque no es válido \n",mpi_rank,status->MPI_SOURCE);
    return false;
}


//Envia el bloque minado a todos los nodos
void broadcast_block(const Block *block){
    //No enviar a mí mismo
    //mando desde el de la derecha en adelante, supongo q es un orden distinto... no?
    int dest;
    for (int i =1; i <total_nodes; i++) {
        dest=(mpi_rank+i)%total_nodes;
        MPI_Send(block, 1, *MPI_BLOCK, dest, TAG_NEW_BLOCK, MPI_COMM_WORLD);
    }
}
void lock(){
    bool expected = false;
    while (!probando.compare_exchange_weak(expected, true)){
        expected = false;
    }
}
void unlock(){
    probando=false;
}
//Proof of work
//TODO: Advertencia: puede tener condiciones de carrera
void* proof_of_work(void *ptr){
    string hash_hex_str;
    Block block;
    unsigned int mined_blocks = 0;
    while(true){

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
        //Contar la cantidad de ceros iniciales (con el nuevo nonce)
        if(solves_problem(hash_hex_str)){
            //Verifico que no haya cambiado mientras calculaba
            lock();
            if(last_block_in_chain->index < block.index){
                mined_blocks += 1;
                *last_block_in_chain = block;
                strcpy(last_block_in_chain->block_hash, hash_hex_str.c_str());
                last_block_in_chain->created_at = static_cast<unsigned long int> (time(NULL));
                node_blocks[hash_hex_str] = *last_block_in_chain;
                printf("[%d]Agregué un producido con index %d\n",mpi_rank,last_block_in_chain->index);

                //TODO: Mientras comunico, no responder mensajes de nuevos nodos
                broadcast_block(last_block_in_chain);
            }
            unlock();
        }

    }

    return NULL;
}

void mandar_cadena(char block_hash[HASH_SIZE], const MPI_Status *status){
    Block  block=node_blocks[block_hash];
    uint size= min(VALIDATION_BLOCKS,(int)block.index);
    Block *blockchain = new Block[size];
    // blockchain[0]=*block;
    //mandar los bloques en orden inverso asi despues "alice" puede fijarse en orden si lesirve
    for (size_t i = 0; i < size; i++) {
        blockchain[i]= node_blocks[block_hash];
        block_hash =node_blocks[block_hash].previous_block_hash;
    }
    MPI_Send(blockchain, size, *MPI_BLOCK, status->MPI_SOURCE, TAG_CHAIN_RESPONSE, MPI_COMM_WORLD);

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
    char block_hash[HASH_SIZE];
    Block *block = new Block;
    while(true){
        //TODO: Recibir mensajes de otros nodos
        MPI_Status status;
        MPI_Probe(MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
        auto tag= status.MPI_TAG;
        if (tag ==TAG_NEW_BLOCK){
            MPI_Recv(block, 1, *MPI_BLOCK,  status.MPI_SOURCE, TAG_NEW_BLOCK, MPI_COMM_WORLD, &status);
            //TODO: Si es un mensaje de nuevo bloque, llamar a la función
            // validate_block_for_chain con el bloque recibido y el estado de MPI
            lock();
            validate_block_for_chain(block, &status);
            unlock();
        }else if(tag==TAG_CHAIN_HASH){
            //TODO: Si es un mensaje de pedido de cadena,
            //responderlo enviando los bloques correspondientes
            MPI_Recv(block_hash, HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, TAG_CHAIN_HASH, MPI_COMM_WORLD, &status);
            mandar_cadena(block_hash, &status);
        }
    }

    delete last_block_in_chain;
    delete block;
    return 0;
}
