/************************************************************
Copyright (C).
FileName: XML_parallel.c
Author: Jack
Version : V3.0
Date: 01/18/2015
Description: This program could execute the XPath to get some necessary information from a large XML dataset. 
It could also divide it into several parts and deal with each part in parallel.
History: 
<author> <time> <version> <desc>
1 Jack 01/03/2015 V1.0 build the first version which could implement the main functions of XML parallel processing
2 Jack 01/13/2015 V2.0 optimize the code for split phase to load XML file into memory, not read them directly from original file.
3 Jack 01/18/2015 V3.0 the main function takes number-of-threads as an input parameter without asking the size of a partition, 
make some optimizations on the split phase to get a better performance and merge the sequential version of this algorithm into one program.
***********************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <malloc.h>
#include <sys/time.h>

/*data structure for each thread*/
#define MAX_THREAD 10
pthread_t thread[MAX_THREAD]; 
int thread_args[MAX_THREAD];
int finish_args[MAX_THREAD];

/*data structure for automata*/
typedef struct{
	int start;
	char * str;
	int end;
	int isoutput; 
}Automata;

#define MAX_SIZE 50
Automata stateMachine[MAX_SIZE];   //save automata for XPath

int stateCount=0; //the number of states for XPath
int machineCount=1; //the number of nodes for automata

/*data structure for each tree*/
#define MAX_OUTPUT 20000
typedef struct Node{
    int state;
    struct Node ** children;
    struct Node * start_node;
    struct Node * finish_node;
    char * output;
    int hasOutput;
    struct Node * parent;
    int isLeaf;
}Node;

typedef struct QueueEle{
	Node* node;
	int layer;
}QueueEle;

Node* start_root[MAX_THREAD];   //start tree for each thread
Node* finish_root[MAX_THREAD];   //finish tree for each thread

/*data structure for files in each thread*/
char * buffFiles[MAX_THREAD]; 

/*data structure for elements in XML file*/
typedef struct
{
    char *p;
    int len;
}
xml_Text;

typedef enum {
    xml_tt_U, /* Unknow */
    xml_tt_H, /* XML Head <?xxx?>*/
    xml_tt_E, /* End Tag </xxx> */
    xml_tt_B, /* Start Tag <xxx> */
    xml_tt_BE, /* Tag <xxx/> */
    xml_tt_T, /* Content for the tag <aaa>xxx</aaa> */
    xml_tt_C, /* Comment <!--xx-->*/
    xml_tt_ATN, /* Attribute Name <xxx id="">*/
    xml_tt_ATV, /* Attribute Value <xxx id="222">*/
    xml_tt_CDATA/* <![CDATA[xxxxx]]>*/
}
xml_TokenType;

typedef struct
{
    xml_Text text;
    xml_TokenType type;
}
xml_Token;

#define MAX_LINE 100
static char multiExpContent[MAX_THREAD][MAX_LINE];  //save for multi-line explanations
static char multiCDATAContent[MAX_THREAD][MAX_LINE]; //save for multi-line CDATA

#define MAX_ATT_NUM 50
char tokenValue[MAX_ATT_NUM][MAX_ATT_NUM]={"UNKNOWN","HEAD","NODE_END","NODE_BEGIN","NODE_BEGIN_END","TEXT","COMMENT","ATTRIBUTE_NAME","ATTRIBUTE_VALUE","CDATA"};
char defaultToken[MAX_ATT_NUM]="WRONG_INFO";

/*data structure for mapping result*/
typedef struct ResultSet
{
	int begin;
	int begin_stack[MAX_SIZE];
	int topbegin;
	int end;
	int end_stack[MAX_SIZE];
	int topend;
	char* output;
	int hasOutput;
}ResultSet;


/*before thread creation*/
int load_file(char* file_name); //load XML into memory(only used for sequential version)
int split_file(char* file_name, int n);  //split XML file into several parts and load them into memory
char* ReadXPath(char* xpath_name);  //load XPath into memory
void createAutoMachine(char* xmlPath);   //create automachine for XPath.txt

/*main functions for each thread*/
void createTree_first(int start_state); //create tree for the first thread
void createTree(int thread_num); //create tree for other threads
void print_tree(Node* tree,int layer); //print the structure for each tree
void add_node(Node* node, Node* root);  //insert a new node into finish tree
void push(Node* node, Node* root, int nextState); //push new element into stack
int checkChildren(Node* node);  //return value--the number of children -1--no child
void pop(char * str, Node* root); //pop element due to end_tag e.g</d>
int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);  //parse and deal with every element in an xmlText, return value:0--success -1--error 1--multiline explantion 2--multiline CDATA

/*functions called by each thread*/
char* substring(char *pText, int begin, int end);
char* convertTokenTypeToStr(xml_TokenType type); //get the type for each element
int xml_initText(xml_Text *pText, char *s);
int xml_initToken(xml_Token *pToken, xml_Text *pText);
char* ltrim(char *s); //reduct blank from left
int left_null_count(char *s);  //calculate the number of blanket for each string

/*get and merge the mappings for the result*/
ResultSet getresult(int n);
void print_result(ResultSet set);


/*************************************************
Function: int split_file(char* file_name,int n);
Description: split a large file into several parts according to the number of threads for this program, while keeping the split XML files into the memory
Called By: int main(void);
Input: file_name--the name for the xml file; n--the number of threads for this program
Return: the number of threads(start with 0); -1--can't open the XML file
*************************************************/
int split_file(char* file_name,int n)
{
	FILE *fp;
    int i,j,k,one_size;
    int size;
    fp = fopen (file_name,"rb");
    if (fp==NULL) { return -1;}
    fseek (fp, 0, SEEK_END);   
    size=ftell (fp);
    rewind(fp);
    one_size=(size/n)+1;
    n=n-1;
    char ch=-1;
    int temp_one_size=0;
    int s;
    for (i=0;i<n;i++)
    {
    	s=0;
        buffFiles[i]=(char*)malloc((one_size+MAX_LINE)*sizeof(char));
        
        if(ch!=-1)
        {
    	    buffFiles[i][0]=ch;
    	    buffFiles[i][1]='\0';
    	    char * buff=(char*)malloc((one_size)*sizeof(char));
    	    k = fread (buff,1,one_size-1,fp);
    	    buff[one_size-1]='\0'; 
    	    buffFiles[i]=strcat(buffFiles[i],buff);
    	    free(buff);
	    }
	    else
	    {
	    	k = fread (buffFiles[i],1,one_size,fp);
		}
        /*skip the default size to look for the next open angle bracket*/
        ch=fgetc(fp);
        while(ch!='<')
        {
        	buffFiles[i][one_size+(s++)]=ch;
        	ch=fgetc(fp);
        	temp_one_size++;
		}
		buffFiles[i][one_size+s]='\0';
    }
    j = size % one_size-temp_one_size;
    if (j!=0) {
    buffFiles[n]=(char*)malloc((j+1)*sizeof(char));
    if(ch!=-1)
    {
    	buffFiles[n][0]=ch;
    	buffFiles[n][1]='\0';
    	char * buff=(char*)malloc((j+1)*sizeof(char));
    	k = fread (buff,1,j,fp);
    	buff[j+1]='\0'; 
    	buffFiles[n]=strcat(buffFiles[n],buff);
    	buffFiles[n][j]='\0'; 
    	free(buff);
	}
    else
    {
    	k = fread (buffFiles[n],1,j,fp);
        buffFiles[n][j]='\0';  
	}
	
    }
    close(fp);
    return n;
}

/*************************************************
Function: int load_file(char* file_name);
Description: load the XML file into memory(only used for sequential version)
Called By: int main(void);
Input: file_name--the name for the xml file
Return: 0--load successful; -1--can't open the XML file
*************************************************/
int load_file(char* file_name)
{
	FILE *fp;
    int i,j,k,n;
    int size;
    fp = fopen (file_name,"rb");
    if (fp==NULL) { return -1;}
    fseek (fp, 0, SEEK_END);   
    size=ftell (fp);
    rewind(fp);
    buffFiles[0]=(char*)malloc((size+1)*sizeof(char));
    k = fread (buffFiles[0],1,size,fp);
    buffFiles[0][size]='\0'; 
    close(fp);
    return 0;
}

/*************************************************
Function: char* ReadXPath(char* xpath_name);
Description: load XPath from related file
Called By: int main(void);
Input: xpath_name--the name for the XPath file
Return: the contents in the Xpath file; error--can't open the XPath file
*************************************************/
char* ReadXPath(char* xpath_name)
{
	FILE *fp;
	char* buf=(char*)malloc(MAX_LINE*sizeof(char));
	char* xpath=(char*)malloc(MAX_LINE*sizeof(char));
	xpath=strcpy(xpath,"");
	if((fp = fopen(xpath_name,"r")) == NULL)
    {
        xpath=strcpy(xpath,"error");
    }
    else{
    	while(fgets(buf,MAX_LINE,fp) != NULL)
    	{
    		xpath=strcat(xpath,buf);
		}
	}
	free(buf);
    return xpath;
}

/*************************************************
Function: void createAutoMachine(char* xmlPath);
Description: create an automata by the XPath Query command
Called By: int main(void);
Input: xmlPath--XPath Query command
*************************************************/
void createAutoMachine(char* xmlPath)
{
	char seps[] = "/"; 
	char *token = strtok(xmlPath, seps); 
	while(token!= NULL) 
	{
		stateCount++;
		stateMachine[machineCount].start=stateCount;
		stateMachine[machineCount].str=(char*)malloc((strlen(token)+1)*sizeof(char));
		stateMachine[machineCount].str=strcpy(stateMachine[machineCount].str,token);
		stateMachine[machineCount].end=stateCount+1;
		stateMachine[machineCount].isoutput=0;
		machineCount++;
		if(stateCount>=1)
		{
			stateMachine[machineCount].start=stateCount+1;
			stateMachine[machineCount].str=(char*)malloc((strlen(stateMachine[machineCount-1].str)+2)*sizeof(char));
			stateMachine[machineCount].str=strcpy(stateMachine[machineCount].str,"/");
			stateMachine[machineCount].str=strcat(stateMachine[machineCount].str,stateMachine[machineCount-1].str);
			stateMachine[machineCount].end=stateCount;
			stateMachine[machineCount].isoutput=0;
		}
		token=strtok(NULL,seps);  
		if(token==NULL)
		{
			stateMachine[machineCount-1].isoutput=1;
			stateMachine[machineCount].isoutput=1;
		}
		else machineCount++;
	}
    stateCount++;
}

/*************************************************
Function: void createTree(int thread_num);
Description: initiate a stack tree for other thread other than the first thread
Called By: void *main_thread(void *arg);for sequential version, called by void main_function();
Input: thread_num--the number of the thread
*************************************************/
void createTree(int thread_num)
{
	start_root[thread_num]=(Node*)malloc(sizeof(Node));
	finish_root[thread_num]=(Node*)malloc(sizeof(Node));
	start_root[thread_num]->children=(Node**)malloc(stateCount*sizeof(Node*));
	finish_root[thread_num]->children=(Node**)malloc(stateCount*sizeof(Node*));
	finish_root[thread_num]->state=-1;
	int i,j;
	for(i=0;i<=stateCount;i++)
	{
		start_root[thread_num]->children[i]=(Node*)malloc(sizeof(Node));
		finish_root[thread_num]->children[i]=(Node*)malloc(sizeof(Node));
		for(j=0;j<=stateCount;j++)
		{
			start_root[thread_num]->children[i]->children=(Node**)malloc(stateCount*sizeof(Node*));
			finish_root[thread_num]->children[i]->children=(Node**)malloc(stateCount*sizeof(Node*));
			
			start_root[thread_num]->children[i]->children[j]=NULL;
			finish_root[thread_num]->children[i]->children[j]=NULL;
		}
		finish_root[thread_num]->children[i]->hasOutput=0;
		finish_root[thread_num]->children[i]->output=(char*)malloc(MAX_OUTPUT*sizeof(char));
		finish_root[thread_num]->children[i]->output=strcpy(finish_root[thread_num]->children[i]->output,"");
		start_root[thread_num]->children[i]->state=i;
		start_root[thread_num]->children[i]->parent=start_root[thread_num];
		finish_root[thread_num]->children[i]->state=i;
		finish_root[thread_num]->children[i]->parent=finish_root[thread_num];
		start_root[thread_num]->children[i]->finish_node=finish_root[thread_num]->children[i];
		finish_root[thread_num]->children[i]->start_node=start_root[thread_num]->children[i];
		start_root[thread_num]->children[i]->isLeaf=1;
		finish_root[thread_num]->children[i]->isLeaf=1;
	}
	for(i=0;i<=stateCount;i++)
	{
		for(j=0;j<=stateCount;j++)
		{
			start_root[thread_num]->children[i]->children[j]=NULL;
			finish_root[thread_num]->children[i]->children[j]=NULL;
		}
	}
}

/*************************************************
Function: void createTree_first(int start_state);
Description: initiate a stack tree for the first thread, the start_state is 1
Called By: void *main_thread(void *arg);for sequential version, called by void main_function();
Input: start_state is 1
*************************************************/
void createTree_first(int start_state)
{
	int thread_num=0;
	start_root[thread_num]=(Node*)malloc(sizeof(Node));
	finish_root[thread_num]=(Node*)malloc(sizeof(Node));
	start_root[thread_num]->children=(Node**)malloc(stateCount*sizeof(Node*));
	finish_root[thread_num]->children=(Node**)malloc(stateCount*sizeof(Node*));
	start_root[thread_num]->isLeaf=0;
	finish_root[thread_num]->isLeaf=0;
	int i,j;
	for(i=0;i<=stateCount;i++)
	{
		if(i==start_state)
		{
			start_root[thread_num]->children[i]=(Node*)malloc(sizeof(Node));
		    finish_root[thread_num]->children[i]=(Node*)malloc(sizeof(Node));
		    finish_root[thread_num]->children[i]->hasOutput=0;
		    finish_root[thread_num]->children[i]->output=(char*)malloc(MAX_OUTPUT*sizeof(char));
		    finish_root[thread_num]->children[i]->output=strcpy(finish_root[thread_num]->children[i]->output,"");
		    for(j=0;j<=stateCount;j++)
		    {
			    start_root[thread_num]->children[i]->children=(Node**)malloc(stateCount*sizeof(Node*));
			    finish_root[thread_num]->children[i]->children=(Node**)malloc(stateCount*sizeof(Node*));
			    start_root[thread_num]->children[i]->children[j]=NULL;
			    finish_root[thread_num]->children[i]->children[j]=NULL;
		    }
		    start_root[thread_num]->children[i]->state=i;
		    start_root[thread_num]->children[i]->parent=start_root[thread_num];
		    finish_root[thread_num]->children[i]->state=i;
		    finish_root[thread_num]->children[i]->parent=finish_root[thread_num];
		    start_root[thread_num]->children[i]->finish_node=finish_root[thread_num]->children[i];
		    finish_root[thread_num]->children[i]->start_node=start_root[thread_num]->children[i];
		    start_root[thread_num]->children[i]->isLeaf=1;
		    finish_root[thread_num]->children[i]->isLeaf=1;
		}
		else
		{
			start_root[thread_num]->children[i]=NULL;
		    finish_root[thread_num]->children[i]=NULL;
		}		
	}
	for(j=0;j<=stateCount;j++)
	{
		start_root[thread_num]->children[start_state]->children[j]=NULL;
		finish_root[thread_num]->children[start_state]->children[j]=NULL;
	}
}

/*************************************************
Function: void print_tree(Node* tree,int layer);
Description: print the structure of the tree for each layer
Called By: void *main_thread(void *arg); for sequential version, called by void main_function();
Input: tree--the start or finish stack tree; layer--the initial layer of the tree, default is 0
*************************************************/
void print_tree(Node* tree,int layer)
{
	QueueEle element;
	Node* node=tree;
	int i=0;
	layer++;
	QueueEle queue[MAX_SIZE];
	int top=-1;
	int buttom=-1;
	int lastlayer=0;
	printf("the 0th layer only includes the root node and its state is -1;");
	for(i=0;i<=stateCount;i++)
	{
		
		if(node->children[i]!=NULL)
		{
		    element.node=node->children[i];
			element.layer=layer;
			queue[(top+1)%MAX_SIZE]=element;
			top=(top+1)%MAX_SIZE;
		}
	}
	while((top-buttom)%MAX_SIZE>0)
	{
		element=queue[(buttom+1)%MAX_SIZE];
		node=element.node;
		layer=element.layer;
		if(layer>lastlayer)
		{
			printf("\nthe %d layer is: ",layer);
			lastlayer=layer;
		}
		printf("state %d  parent %d;",node->state,node->parent->state);
		buttom=(buttom+1)%MAX_SIZE;
		if(node->children!=NULL){
		for(i=0;i<=stateCount;i++)
		{
			if(node->children[i]!=NULL)
		    {
				element.node=node->children[i];
				element.layer=layer+1;
				queue[(top+1)%MAX_SIZE]=element;
				top=(top+1)%MAX_SIZE;
		    }
		}
	}
	}
	printf("\n\n");
}

/*************************************************
Function: void add_node(Node* node, Node* root);
Description: add a node into the tree. Each tree node has at most one child for each state. 
If a transition causes two child nodes to have the same symbol, then two nodes would be merged
Called By: void push(Node* node, Node* root, int nextState);void pop(char * str, Node* root);
Input: node--the current node would be added into the tree; root--the root of the tree.
*************************************************/
void add_node(Node* node, Node* root) 
{
	Node* rtchild;
	int i,j;
    if(root->children[node->state]!=NULL)
    {
    	/*merge the children of node into the original node in the tree*/
         for(i=0;i<=stateCount;i++)
         {
         	if(node->children[i]!=NULL ){
         		rtchild=node->children[i];
         		node->children[i]=NULL;
         		rtchild->parent=NULL;
         		add_node(rtchild,root->children[node->state]);
		    }
         }
    }
    else {
    	root->children[node->state]=node;
    	node->parent=root;
	}
    
}

/*************************************************
Function: void push(Node* node, Node* root, int nextState) ;
Description: push new element into stack tree
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: node--the current node of the tree; root--the root of the tree; 
nextState--the state for the next node which would be pushed on top of the current node;
*************************************************/
void push(Node* node, Node* root, int nextState) 
{
    Node* n;
    int i;
    n=(Node*)malloc(sizeof(Node));
    n->state=node->state;
    n->hasOutput=0;
    n->output=(char*)malloc(MAX_OUTPUT*sizeof(char));
    n->output=strcpy(n->output,"");
    n->start_node=NULL;
    n->finish_node=NULL;
    node->state=nextState;
    n->children=node->children;
    if(node->isLeaf==1)
    {
    	node->start_node->finish_node=n;
    	n->start_node=node->start_node;
    	n->isLeaf=1;
    	node->start_node=NULL;
    	node->isLeaf=0;
	}
    for(i=0;i<=stateCount;i++)
    {
    	if(n->children[i]!=NULL)
    	{
    		n->children[i]->parent=n;
		}
	}
    node->start_node=NULL;
    node->children=NULL;
    node->children=(Node**)malloc(stateCount*sizeof(Node*));
    node->children[n->state]=n;
    n->parent=node;
 
    for(i=0;i<=stateCount;i++ )
    {
    	if(i!=n->state)
    	   node->children[i]=NULL;
	}

    add_node(node,root);
}

/*************************************************
Function: int checkChildren(Node* node);
Description: check if a node has children
Called By: void pop(char * str, Node* root);
Input: node--the original node;
Return: the number of children; -1--no child
*************************************************/
int checkChildren(Node* node) 
{
	int i;
	for(i=0;i<=stateCount;i++)
	{
		if(node->children[i]!=NULL)
		{
			break;
		}
	}
	if(i<=stateCount)  return i;
	else return -1;
}

/*************************************************
Function: void pop(char * str, Node* root);
Description: if type of the xml element is End Tag(e.g </xxx>) and the content of the tag could be found in the automata, 
then this function would delete the related node from the finishing stack tree. If no such node exists, a new node is created 
in the start tree, thus pushing the next state on the starting stack tree.
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: str-the content of the xml element; root-the root of the tree;
*************************************************/
void pop(char * str, Node* root) //pop element due to end_tag e.g</d>
{
    int i,j,begin,next;
    int flag=0;
    Node * n;
    int isoutput=0;
    int k;
    for(j=machineCount;j>=1;j=j-2)
    {
        if(strcmp(str,stateMachine[j].str)==0)
        {
            break;
		}
	}
	if(j>=1)
	{
		begin=stateMachine[j].start;
		next=stateMachine[j].end;
		j=begin;
		if(j<=stateCount)
		{
			n=root->children[j]->children[next];  //for state j
			if(n!=NULL&&n->state==next)
			{
				if(root->children[j]->hasOutput==1)
				{					
					if(n->hasOutput==1)
					    n->output=strcat(n->output," ");
					else n->hasOutput=1;					
					n->output=strcat(n->output,root->children[j]->output);
					if(root->children[j]->output!=NULL) free(root->children[j]->output);
					root->children[j]->output=NULL;
					root->children[j]->hasOutput=0;
				}
				n->parent=NULL;
				root->children[j]->children[next]=NULL;
				add_node(n,root);

				if(checkChildren(root->children[j])==-1)
				{
					if(root->children[j]!=NULL) free(root->children[j]);
					root->children[j]=NULL;
					flag=1;
				}
				if(root->children[0]!=NULL)
				{
					for(i=stateCount;i>=0;i--)  //for state0
				    {
				        	
					    if(root->children[0]->children[i]!=NULL)
					    {

					        n=root->children[0]->children[i];
					        n->parent=NULL;
					        root->children[0]->children[i]=NULL;
					        if(i==0){
					            if(root->children[0]!=NULL) {
								    free(root->children[0]);
								}
					            root->children[0]=NULL;
							}
					        add_node(n,root);
						}
			     	}
			   } 
			}
			else if(flag==0) //not in final tree, add it into the start tree
			   {
			   	    n=root->children[begin]->start_node;
			   	    if(n!=NULL)
				    {
				    	if(root->children[next]->start_node!=NULL)
				    	{
				            Node* ns=(Node*)malloc(sizeof(Node));
                            ns->state=begin;
                            ns->parent=n->parent;
                            ns->children=NULL;
                            ns->start_node=NULL;
				    		n->children[next]=root->children[next]->start_node;  //for pop node
				    		root->children[next]->start_node->parent->children[next]=NULL;
                            root->children[next]->start_node->parent=n;
                            ns->finish_node=root->children[begin];
                            if(root->children[begin]->hasOutput==1)
				            {
					            root->children[next]->hasOutput=1;
					            root->children[next]->output=strcpy(ns->finish_node->output,root->children[begin]->output);
					            
					            root->children[begin]->hasOutput=0;
				            }
                            root->children[begin]->start_node=ns;
                            //for pop node 0
                            n=root->children[0]->start_node;
                            n->children=(Node**)malloc(sizeof(Node));
                            for(k=0;k<=stateCount;k++)
                            {
                            	n->children[k]=NULL;
							}
                            ns=NULL;
                            
                            if(n!=NULL)
                            {
                            	for(i=0;i<=stateCount;i++)
                            	{
                            		if(i==next)
									{
										continue;
									} 
                            		if(i==0)
                            		{
                            			ns=(Node*)malloc(sizeof(Node));
                                        ns->state=i;
                                        ns->parent=n;
                                        ns->children=NULL;
                                        ns->start_node=NULL;
                                        n->children[i]=ns;
                                        ns->finish_node=root->children[0];
                                        root->children[0]->start_node=ns;
									}
										
									if(i!=0){
                                        if(i!=begin)
										{
                                        	root->children[i]->start_node->parent->children[i]=NULL;
										}
                                        root->children[i]->start_node->parent=n;
										n->children[i]=root->children[i]->start_node;  //for next state
                                    }
								}
							}
						}
					}
				}
		}
	}      
}

/*************************************************
Function: char * convertTokenTypeToStr(xml_TokenType type);
Description: convert the XML token type from digit to the real string for output
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: type--the enumeration for the type of XML
Return: the output string for this type
*************************************************/
char * convertTokenTypeToStr(xml_TokenType type)
{

	if(type<MAX_ATT_NUM) return tokenValue[type];
	else return defaultToken;
}

/*************************************************
Function: int xml_initText(xml_Text *pText, char *s);
Description: initiate a xml_Text for a string loading from original XML file
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pText--the xml_Text element waiting to be initialized; s--the XML string;
Output: pText--the initialized xml_Text
Return: 0--success
*************************************************/
int xml_initText(xml_Text *pText, char *s)
{
    pText->p = s;
    pText->len = strlen(s);
    return 0;
}

/*************************************************
Function: xml_initToken(xml_Token *pToken, xml_Text *pText);
Description: initiate a xml_Token for a initialized xml_Text
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pToken--the xml_Token element waiting to be initialized; pText--input xml_Text;
Output: pToken--the initialized xml_Token
Return: 0--success
*************************************************/
int xml_initToken(xml_Token *pToken, xml_Text *pText)
{
    pToken->text.p = pText->p;
    pToken->text.len = 0;
    pToken->type = xml_tt_U;
    return 0;
}

/*************************************************
Function: int xml_print(xml_Text *pText, int begin, int end);
Description: print the substring of a xml_Text
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pText--input xml_Text; begin--start position; end--end position;
Output: the substring of a xml_Text
Return: 0--success
*************************************************/
int xml_print(xml_Text *pText, int begin, int end)
{
    int i;
    char * temp=pText->p;
    temp = ltrim(pText->p);
    int j=0;
    for (i = begin; i < end; i++)
    {
        putchar(temp[i]);
    }
    return 0;
}

/*************************************************
Function: char* substring(char *pText, int begin, int end);
Description: print the substring of the original string
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pText--the original string; begin--start position; end--end position;
Return: the final string
*************************************************/
char* substring(char *pText, int begin, int end)
{
    int i,j;
    char * temp=pText;
    temp = ltrim(pText);
    char* temp1=(char*)malloc((end-begin+1)*sizeof(char));
    for (j = 0,i = begin; i < end; i++,j++)
    {
        temp1[j]=temp[i];
    }
    temp1[j]='\0';
    return temp1;
}

/*************************************************
Function: char * ltrim(char *s);
Description: remove the left blankets of a string
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: s--the original string; 
Return: the final substring
*************************************************/
char * ltrim(char *s)
{
     char *temp;
     temp = s;
     while((*temp == ' ' || *temp=='\t' )&&temp){*temp++;}
     return temp;
}

/*************************************************
Function: int left_null_count(char *s);
Description: calculate the number of blanket for each string
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: s--the original string; 
Return: the number of blanket for each string
*************************************************/
int left_null_count(char *s)  
{
	 int count=0;
     char *temp;
     temp = s;
     while((*temp == ' ' || *temp=='\t' )&&temp){*temp++; count++;}
     return count;
}

/*************************************************
Function: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Description: the function could be called by each thread, dealing with each line of the file. Besides, this function could identify the following elements, 
which include XML head, Start Tag(e.g <xxx>), End Tag(e.g </xxx>), Tag(e.g <xxx/>), Content for the Tag, XML Explanation, Attribute Name for Tag, 
Attribute Value for Tag, Content for CDATA element. Each element would be processed according to its type. 
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pText-the content of the xml file; pToken-the type of the current xml element; multilineExp-whether the current line of the xml file is the multiline explanation; 
multilineCDATA-- whether the current line of the xml file is the multiline CDATA; thread_num-the number of the thread; 
Return: 0--success -1--error 1--multiline explantion 2--multiline CDATA
*************************************************/
int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num)  
{
	Node * tempnode=finish_root[thread_num];
    char *start = pToken->text.p + pToken->text.len;
    char *p = start;
    char *end = pText->p + pText->len;
    int state = 0;
    int templen = 0;
    if(multilineExp == 1) state = 10;   //1--multiline explantion  0--single line explantion
    if(multilineCDATA == 1) state = 17; //1--multiline CDATA 0--single CDATA
    int j,a;
    Node* node;

    pToken->text.p = p;
    pToken->type = xml_tt_U;
    
    for (; p < end; p++)
    {
        switch(state)
        {
            case 0:
               switch(*p)
               {
                   case '<':
                   	   
                       state = 1;
                       break;
                   case ' ':
                   	   state = 0;
                   	   break;
                   default:
                       state = 7;
                       break; 
               }
            break;
            case 1:
               switch(*p)
               {
                   case '?':
                       state = 2;
                       break;
                   case '/':
                       state = 4;
                       break;
                   case '!':
                   	   state = 8;
                   	   break;
                   case ' ':
                   	   state = -1;
                   	   break;
                   default:
                       state = 5;
                       break;
               }
            break;
            case 2:
               switch(*p)
               {
                   case '?':
                       state = 3;
                       break;
                   default:
                       state = 2;
                       break;
               }
            break;
            case 3:
               switch(*p)
               {
               	   putchar(*p);
                   case '>':                        /* Head <?xxx?>*/
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_H;
                       //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                       //printf("%s","content=");
                       templen = pToken->text.len;
                       //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                       //xml_print(&pToken->text, 0 ,pToken->text.len);
                       //printf(";\n\n");
                       pToken->text.p = start + templen;
                       start = pToken->text.p;
                       state = 0;
                       break;
                   default:
                       state = -1;
                       break;
               }
               break;
            case 4:
                switch(*p)
                {
                   case '>':              /* End </xxx> */
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_E;
                       //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                       //printf("%s","content=");
                       //xml_print(&pToken->text, 2 , pToken->text.len-1);
                       //printf(";\n\n");
                       
                       char* subs=substring(pToken->text.p , 1 , pToken->text.len-1-left_null_count(pToken->text.p));
					   if(subs!=NULL){
					       for(j=machineCount;j>=1;j=j-2)
                           {
                               if(strcmp(subs,stateMachine[j].str)==0)
                                  break;
	                       }
	                       if(j>=1){
                               pop(subs,finish_root[thread_num]);
                           }
                           free(subs);
                       }
                       pToken->text.p = start + pToken->text.len;
                       start = pToken->text.p;
                       state = 0;
                       break;
                   case ' ':
                   	   state = -1;
                   	   break;
                   default:
                       state = 4;
                       break;
                }
                break;
            case 5:
                switch(*p)
                {
                   case '>':               /* Begin <xxx> */
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_B;
                       if(pToken->text.len-1 >= 1){
                       	   //layer++;
                       	   //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                           //printf("%s","content=");
                           templen = pToken->text.len;
                           //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                           
                       	   //xml_print(&pToken->text , 1 , pToken->text.len-1);
                       	   
                           //printf(";\n\n");
                           char* sub=substring(pToken->text.p , 1 , pToken->text.len-1-left_null_count(pToken->text.p));
                           
                           for(j=machineCount-1;j>=1;j=j-2)
                           {
                           	   if(strcmp(sub,stateMachine[j].str)==0)
                           	   {
                           	      break;
							   }
						   }
                            if(sub!=NULL)  free(sub);
						   if(j>=1)  
						   {
						   	    int a;
						   	    for(a=0;a<=stateCount;a++)  //for state0
						   	    {
						   	    	if(a!=stateMachine[j].start&&finish_root[thread_num]->children[a]!=NULL)
						   	    	{
						   	    		node=finish_root[thread_num]->children[a];
                                        finish_root[thread_num]->children[a]=NULL;
                                        push(node,finish_root[thread_num],0);
									}
								}
								int begin=stateMachine[j].start;
								int end=stateMachine[j].end;
								node=finish_root[thread_num]->children[begin];
								finish_root[thread_num]->children[begin]=NULL;
								push(node,finish_root[thread_num],end);   //for state j								
						   }
					   }
					   else templen = 1;
                       pToken->text.p = start + templen;
                       start = pToken->text.p;
                       
                       state = 0;
                       break;
                   case '/':
                       state = 6;
                       break;
                   case ' ':                 /* Begin <xxx> */
                   	   pToken->text.len = p - start + 1;
                   	   templen = 0;
                      // pToken->type = xml_tt_B;
                       if(pToken->text.len-1 >= 1)
                       {
                       	   //layer++;
                       	   //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                       	   //printf("%s","content=");
                       	   templen = pToken->text.len;
                       	   //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                       	   //xml_print(&pToken->text , 1 , pToken->text.len-1);
                       	   //printf(";\n\n");
                       	   char* sub=substring(pToken->text.p , 1 , pToken->text.len-1-left_null_count(pToken->text.p));  
                           for(j=machineCount-1;j>=1;j=j-2)
                           {
                           	   if(strcmp(sub,stateMachine[j].str)==0)
                           	   {
                           	      break;
							   }
						   }
						   if(sub) free(sub);
						   if(j>=1)   
						   {
						   	    int a;
						   	    for(a=0;a<=stateCount;a++)  //for state0
						   	    {
						   	    	if(a!=stateMachine[j].start&&finish_root[thread_num]->children[a]!=NULL)
						   	    	{
						   	    		node=finish_root[thread_num]->children[a];
                                        finish_root[thread_num]->children[a]=NULL;
                                        push(node,finish_root[thread_num],0);
									}
								}
								int begin=stateMachine[j].start;
								int end=stateMachine[j].end;
								node=finish_root[thread_num]->children[stateMachine[j].start];
								finish_root[thread_num]->children[stateMachine[j].start]=NULL;
								push(node,finish_root[thread_num],stateMachine[j].end);   //for state j
						   }
					   }
					    
                       pToken->text.p = start + templen;
                       start = pToken->text.p;
                   	   state = 13;
                   	   break;
                   default:
                       state = 5;
                   break;
                }
                break;
            case 6:
                switch(*p)
                {
                   case '>':   /* Begin End <xxx/> */
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_BE;
                       //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer+1);
                       //printf("%s","content=");
                       templen = pToken->text.len;
                       //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                       //xml_print(&pToken->text , 1 , pToken->text.len-2);
                       //printf(";\n\n");
                       pToken->text.p = start + templen;
                       start = pToken->text.p;
                       state = 0;
                       break;
                   default:
                       state = -1;
                   break;
                } 
                break;
            case 7:
                switch(*p)
                {
                   case '<':     /* Text xxx */
                       p--;
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_T;
                       
                       //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                       //printf("%s","content=");
                       
                       templen = pToken->text.len;
                       //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                       
                       //xml_print(&pToken->text, 0 , pToken->text.len);
                       //printf(";\n\n");
                       for(j=1;j<stateCount;j++)
                       {
                       	   if(finish_root[thread_num]->children[j]!=NULL)
                       	   {
							    break;
					       }
					   }
					   if(tempnode->children[j]->state>1){
					       Node *childnode=tempnode->children[j];
					       
					       if(stateMachine[2*(finish_root[thread_num]->children[j]->state-1)].isoutput==1)
					       {
					       	   if(childnode->hasOutput==1)
								  childnode->output=strcat(childnode->output," ");
					           else childnode->hasOutput=1;
					           char* sub=substring(pToken->text.p , 0 , pToken->text.len-left_null_count(pToken->text.p));
					           childnode->output=strcat(childnode->output,sub);
					            if(sub!=NULL) free(sub);
					       }
				       }
				       pToken->text.p = start + templen;
                       start = pToken->text.p;
                       state = 0;
				       
                       break;
                   
                   default:
                       state = 7;
                       break;
                }
                break;
            case 8:
            	switch(*p)
            	{
            		case '-':
            			state = 9;
            			break;
            		case '[':
            			if(*(p+1)=='C'&&*(p+2)=='D'&&*(p+3)=='A'&&*(p+4)=='T'&&*(p+5)=='A')
            			{
            				state = 16;
            				p += 5;
            				break;
						}
						else
						{
							state = -1;
							break;
						}
            		default:
            			state = -1;
            			break;
				}
			    break;
			case 9:
				switch(*p)
				{
					case '-':
						state = 10;
						break;
					default:
						state = -1;
						break;
				}
			    break;
			case 10:
				switch(*p)
				{
					case '-':
						state = 11;
						break;
					default:
						state = 10;
						break;
				}
			    break;
			case 11:
				switch(*p)
				{
					case '-':
						state = 12;
						break;
					default:
						state = -1;
						break;
				}
			    break;
			case 12:
				switch(*p)
				{
					case '>':                            /* Comment <!--xx-->*/
					    pToken->text.len = p - start + 1;
                        //pToken->type = xml_tt_C;
                        //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                        //printf("%s","content=");
                        templen = pToken->text.len;
                        //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                        if(multilineExp == 1)
                        {
                        	strcat(multiExpContent[thread_num],pToken->text.p);
                            //printf("%s",ltrim(multiExpContent[thread_num]));
                            //printf("\n");
                            memset(multiExpContent[thread_num], 0 , sizeof(multiExpContent[thread_num]));
						}
						/*else{
							xml_print(&pToken->text , 0 , pToken->text.len);
                            printf(";\n\n");
						}*/
						
                        pToken->text.p = start + templen;
                        start = pToken->text.p;
                        state = 0;
						break;
					default:
						state = -1;
						break;
				}
			    break;
			case 13:
				switch(*p)
				{
					case '>':
						state = -1;
						break;
					case '=':                       /*attribute name*/
					    pToken->text.len = p - start + 1;
                        //pToken->type = xml_tt_ATN;
                        //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                        //printf("%s","content=");
                        templen = pToken->text.len;
                        //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                        //xml_print(&pToken->text, 0 , pToken->text.len-1);
                        //printf(";\n\n");
                        pToken->text.p = start + templen;
                        start = pToken->text.p;
						state = 14;
						break;
					default:
						state = 13;
						break;
				}
			    break;
			case 14:
				switch(*p)
				{
					case '"':                                       
                   	    state = 15;
						break;
					case ' ':
						state = 14;
						break;
					default:
						state = -1;
						break;
				}
			    break;	
			case 15:
				switch(*p)
				{
					case '"':                        /*attribute value*/
						pToken->text.len = p - start + 1;
                        //pToken->type = xml_tt_ATV;
                        //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                        //printf("%s","content=");
                        templen = pToken->text.len;
                        //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                        //xml_print(&pToken->text, 1 , pToken->text.len-1);
                        //printf(";\n\n");
                        pToken->text.p = start + templen;
                        start = pToken->text.p;
                        state = 5;
						break;
					default:
						state = 15;
						break;
				}
			    break;
			case 16:
				switch(*p)
				{
					case '[':                                       
                   	    state = 17;
						break;
					default:
						state = -1;
						break;
				}
			    break;	
			case 17:
				switch(*p)
				{
					case ']':                                       
                   	    state = 18;
						break;
					default:
						state = 17;
						break;
				}
			    break;	
			case 18:
				switch(*p)
				{
					case ']':                                       
                   	    state = 19;
						break;
					default:
						state = -1;
						break;
				}
			    break;	
			case 19:
				switch(*p)
				{
					case '>':                                       
                   	    pToken->text.len = p - start + 1;
                        //pToken->type = xml_tt_CDATA;
                        //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                        //printf("%s","content=");
                        templen = pToken->text.len;
                        //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                        if(multilineCDATA == 1)
                        {
                        	strcat(multiCDATAContent[thread_num],pToken->text.p);
                        	/*for( j = 10;j < strlen(multiCDATAContent[thread_num]) - 3;j++)
                        	{
                        		if(multiCDATAContent[thread_num][j] == ']' && multiCDATAContent[thread_num][j+1] == ']') break;
                        		putchar(multiCDATAContent[thread_num][j]);
							}
                            printf("\n");*/
                            memset(multiCDATAContent[thread_num], 0 , sizeof(multiCDATAContent[thread_num]));
						}
						/*else{
							xml_print(&pToken->text , 9 , pToken->text.len-3);
                            printf(";\n\n");
						}*/
						
                        pToken->text.p = start + templen;
                        start = pToken->text.p;
                        state = 0;
						break;
					default:
						state = -1;
						break;
				}
			    break;	
				
            default:  
                state = -1;
                break;
        }
    }
    if(state==-1) {return -1;}
    /*else if(state == 10)
	{
		strcat(multiExpContent[thread_num],ltrim(pToken->text.p));
		return 1;
	} 
	else if(state == 17)
	{
		strcat(multiCDATAContent[thread_num],ltrim(pToken->text.p));
		return 2;
	}*/
	else if(state == 7)
	{
		p--;
        pToken->text.len = p - start + 1;
        if(pToken->text.len>1)
        {
        	//printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
            //printf("%s","content=");
            //xml_print(&pToken->text, 0 , pToken->text.len);
            //printf(";\n\n");
             for(j=1;j<=stateCount;j++)
            {
                if(finish_root[thread_num]->children[j]!=NULL)
                {
                    if(finish_root[thread_num]->children[j]->state<=stateCount)
                    {
					    break;
				    }       	   	   
			    }
		    }
		    if(finish_root[thread_num]->children[j]->state>1){	   
		        if(stateMachine[2*(finish_root[thread_num]->children[j]->state-1)].isoutput==1)
		        {
		        	if(finish_root[thread_num]->children[j]->hasOutput==1)
						finish_root[thread_num]->children[j]->output=strcat(finish_root[thread_num]->children[j]->output," ");
			        else finish_root[thread_num]->children[j]->hasOutput=1;
			        char* sub=substring(pToken->text.p , 0 , pToken->text.len);
			        finish_root[thread_num]->children[j]->output=strcat(finish_root[thread_num]->children[j]->output,sub);
			        if(sub!=NULL) free(sub);
		        }	          
     	    }
        }
		return 0;
	}
    else return 0;
}

/*************************************************
Function: ResultSet getresult(int n) ;
Description: get all the mappings for the stack tree of the related thread, then merged them into one final mapping. 
Called By: int main(void);
Input: n-total number for all the threads; 
Return: the final mapping set
*************************************************/
ResultSet getresult(int n) 
{
	ResultSet final_set,set;
	int i,j,k;
	final_set.topbegin=0;
	final_set.topend=0;
    Node* start_node=start_root[n];
    Node* end_node=finish_root[n];
    final_set.begin=0;final_set.end=0;final_set.output=NULL;final_set.hasOutput=0;
    int start=1;
    Node* root=start_root[0];
    set.begin=start;
    Node* node;
    for(i=0;i<=n;i++)
    {
    	set.begin=start;set.end=0;set.output=NULL;set.hasOutput=0;
    	set.topbegin=0;set.topend=0;
    	node=start_root[i]->children[start];   //the first child for the root
		j=0;
		//deal with the start tree
		if((node==NULL)||(node!=NULL&&node->state>stateCount))
		{
			final_set.begin=-1;
		    break;
	    }
		while(1)
		{
			for(j=0;j<=stateCount;j++)
			{
				if(node->children[j]!=NULL)
				{
			    	node=node->children[j];
			    	set.begin_stack[set.topbegin++]=node->state;
				    break;
			    }
			}
			if(j>stateCount)
			{
				break;
			}
		}
		//deal with final tree
		node=node->finish_node;
		if(node!=NULL&&node->state!=-1)
		{
			set.end_stack[set.topend++]=node->state;
		}
		while(node!=NULL&&node->parent!=NULL&&node->parent->state!=-1)
		{
			node=node->parent;
			set.end_stack[set.topend++]=node->state;
		}
		set.end=set.end_stack[set.topend-1];
		if(node->hasOutput==1&&node->output!=NULL)
		{
			set.output=(char*)malloc((strlen(node->output)+1)*sizeof(char));
			set.output=strcpy(set.output,node->output);
			set.hasOutput=1;
		}
		set.topend--;
		//merge finalset&set
	    if(i>0&&final_set.end!=set.begin)
	    {
		    final_set.begin=-1;
		    break;
	    }
	    else{
		
	        if(set.hasOutput==1)
	        {
		        if(final_set.output==NULL)  {
			        final_set.output=(char*)malloc(MAX_OUTPUT*sizeof(char));
			        memset(final_set.output,0,sizeof(final_set.output));
		        }
		        if(i>0){
		        	final_set.output=strcat(final_set.output," ");
				}  
		        final_set.output=strcat(final_set.output,set.output);
		        final_set.hasOutput=1;
	        }
	        final_set.end=set.end;
	        if(i==0)
	        {
		        final_set.begin=set.begin;
		        for(k=0;k<set.topbegin;k++)
		        {
			        final_set.begin_stack[final_set.topbegin++]=set.begin_stack[k];
		        }
		        for(k=0;k<set.topend;k++)
		        {
			        final_set.end_stack[final_set.topend++]=set.end_stack[k];
		        }
            }
            else{
	            int equal_flag=0;
	            if(final_set.topend==set.topbegin)
	            {
		            for(k=0;k<set.topbegin;k++)
		            {
			            if(final_set.end_stack[set.topbegin-k-1]!=set.begin_stack[k])
			            {
				            equal_flag=1;
				            break;
			            }
		            }
	            }
                if(equal_flag==0)
                {
                	final_set.topend=set.topend;         //end_stack is equal to the current set
	            	for(k=0;k<set.topend;k++)
    	            {
    		            final_set.end_stack[k]=set.end_stack[k];  
		            }
	            }
	            else
	            {
	            	for(k=0;k<set.topend;k++)
    	            {
    		            final_set.end_stack[final_set.topend++]=set.end_stack[k];  //merge
		            }
				}
            }

            start=final_set.end;
    	}
	}
	return final_set;
}

/*************************************************
Function: void print_result(ResultSet set);
Description: print the result mapping set. 
Called By: int main(void);
Input: set-result mapping set; 
*************************************************/
void print_result(ResultSet set)
{
	if(set.begin==-1)
	{
		printf("The mapping for this part is null, please check the XPath command.\n");
		return;
	}
	int i;
	printf("The mapping for this part is: %d,  ",set.begin);
	for(i=0;i<set.topbegin;i++)  
	{
		printf("%d:",set.begin_stack[i]);
	}
    printf(",  ");
	printf("%d,  ",set.end);
	for(i=set.topend-1;i>=0;i--)
	{
		printf("%d:",set.end_stack[i]);
	}
	printf(",  ");
	if(set.output!=NULL)  printf("%s\n",set.output);
	else printf("null");
}

/*************************************************
Function: void *main_thread(void *arg);
Description: main function for each thread. 
Called By: int main(void);
Input: arg--the number of this thread; 
*************************************************/
void *main_thread(void *arg)
{
	int i=(int)(*((int*)arg));
	printf("start to deal with thread %d.\n",i);
	int ret = 0;
    xml_Text xml;
    xml_Token token;               
    int multiExp = 0; //0--single line explanation 1-- multiline explanation
    int multiCDATA = 0; //0--single line CDATA 1-- multiline CDATA
    
    int j;
    if(i==0) 
	{
		createTree_first(1);
	}
    else {
    	createTree(i);
	}
    finish_root[i]->state=-1;
    start_root[i]->state=-1;
    printf("Tree has been created for thread %d.\n",i);
    /*printf("The initial stack tree for the thread %d is shown as follows.\n",i);
	printf("For the start tree\n");
	print_tree(start_root[i],0);
    printf("For the finish tree\n");
    print_tree(finish_root[i],0);*/
    //printf("The results for thread %d are listed as follows:\n",i);
    xml_initText(&xml,buffFiles[i]);
    xml_initToken(&token, &xml);
    ret = xml_process(&xml, &token, multiExp, multiCDATA, i);
    free(buffFiles[i]);
    if(ret==-1)
    {
    	printf("There is something wrong with your XML format, please check it!\n");
    	printf("finish dealing with thread %d.\n",i);
    	return NULL;
	}
    
    /*printf("The final stack tree for the thread %d is shown as follows.\n",i);
	printf("For the start tree\n");
	print_tree(start_root[i],0);
    printf("For the finish tree\n");
    print_tree(finish_root[i],0);*/
    finish_args[i]=1;
    printf("finish dealing with thread %d.\n",i);
	return NULL;
}

/*************************************************
Function: void thread_wait(int n);
Description: waiting for all the threads finish their tasks. 
Called By: int main(void);
Input: n--the number of all threads; 
*************************************************/
void thread_wait(int n)
{
	int t;
	while(1){
		for( t = 0; t <= n; t++)
	    {
	    	if(finish_args[t]==0)
	    	{
	    		break;
			}
	    }
	    if(t>n) break;
	   usleep(10000);
	}   
}

/*************************************************
Function: void main_function();
Description: main function for sequential version. 
Called By: int main(void);
*************************************************/
void main_function()
{
	printf("begin dealing with the state tree.\n");
	int ret = 0;
    xml_Text xml;
    xml_Token token;               
    int multiExp = 0; //0--single line explanation 1-- multiline explanation
    int multiCDATA = 0; //0--single line CDATA 1-- multiline CDATA
    int i=0;
    int j;
    if(i==0) 
	{
		createTree_first(1);
	}
    else {
    	createTree(i);
	}
    finish_root[i]->state=-1;
    start_root[i]->state=-1;
    printf("Tree has been created.\n");
    /*printf("The initial stack tree is shown as follows.\n");
	printf("For the start tree\n");
	print_tree(start_root[i],0);
    printf("For the finish tree\n");
    print_tree(finish_root[i],0);
    printf("The results are listed as follows:\n");*/
    xml_initText(&xml,buffFiles[i]);
    xml_initToken(&token, &xml);
    ret = xml_process(&xml, &token, multiExp, multiCDATA, i);
    free(buffFiles[i]);
    if(ret==-1)
    {
    	printf("There is something wrong with your XML format, please check it!\n");
    	printf("finish dealing with the state tree.\n");
    	return;
	}
    /*printf("The final stack tree is shown as follows.\n");
	printf("For the start tree\n");
	print_tree(start_root[i],0);
    printf("For the finish tree\n");
    print_tree(finish_root[i],0);*/
    finish_args[i]=1;
    printf("finish dealing with the state tree.\n");
}

/*********************************************************************************************/
int main(void)
{
	struct timeval begin,end;
	double duration;
    int ret = 0;
    char* file_name=malloc(MAX_SIZE*sizeof(char));
    file_name=strcpy(file_name,"test.xml");
    char * xpath_name=malloc(MAX_SIZE*sizeof(char));
    xpath_name=strcpy(xpath_name,"XPath.txt");
    printf("Welcome to the XML lexer program! Your file name is test.xml\n\n");
    int choose=0;
    printf("please choose the version for this program (0--sequential version, 1--parallel version)\n");
    scanf("%d",&choose);
    if(choose!=0&&choose!=1)
    {
    	printf("You just input the wrong number, please check it again!\n");
    	exit(1);
	}

    int n;
    if(choose==1)
	{
		printf("please input the number-of-threads for this program (no less than 1 and no more than 10)\n");
        scanf("%d",&n);
        if((n<1)||(n>10))
        {
    	    printf("You just input the wrong number, please check it again!\n");
    	    exit(1);
	    }
	}
    printf("begin to split the file\n");
    gettimeofday(&begin,NULL);
    if(choose==0){
    	n=load_file(file_name);    //load file into memory
	}
    else n=split_file(file_name,n);    //split file into several parts
    printf("finish cutting the file!\n");
    gettimeofday(&end,NULL);   
    duration=1000000*(end.tv_sec-begin.tv_sec)+end.tv_usec-begin.tv_usec; 
    printf("The duration for spliting the file is %lf\n",duration/1000000);
    sleep(1);
        
    if(n==-1)
    {
    	printf("There are something wrong with the xml file, we can not load it. Please check whether it is placed in the right place.\n");
    	exit(1);
	}

	printf("\nbegin to deal with XML file\n");
	gettimeofday(&begin,NULL);
	char* xmlPath="/company/develop/programmer";
	xmlPath=ReadXPath(xpath_name);
	if(strcmp(xmlPath,"error")==0)
	{
		printf("There is something wrong with the XPath file, we can not load it. Please check whether it is placed in the right place.\n");
    	exit(1);
	}
    createAutoMachine(xmlPath);     //create automata by xmlpath
    printf("The basic structure of the automata is (from to end):\n");
    int i,rc;
    char *out=" is an output";
    for(i=1;i<=machineCount;i=i+2)
    {
    	if(i==1){
    		printf("%d",stateMachine[i].start);
		}
		printf(" (str:%s",stateMachine[i].str);
		if(stateMachine[i].isoutput==1)
		{
			printf("%s",out);
		}
		printf(") %d",stateMachine[i].end);
	}
	printf("\n");
	for(i=machineCount;i>0;i=i-2)
    {
    	if(i==machineCount){
    		printf("%d (str:%s) %d",stateMachine[i].start,stateMachine[i].str,stateMachine[i].end);
		}
		else
		{
			printf(" (str:%s) %d",stateMachine[i].str,stateMachine[i].end);
		}	
	}
	printf("\n\n");
	if(choose==0)
	{
		main_function();
	}
	else
	{
		for(i=0;i<=n;i++)
        {
    	    thread_args[i]=i;
    	    finish_args[i]=0;
    	    rc=pthread_create(&thread[i], NULL, main_thread, &thread_args[i]);  //parallel xml processing
    	    if (rc)
            {
                printf("ERROR; return code is %d\n", rc);
                return EXIT_FAILURE;
            }
	    }
	    thread_wait(n);
	}
	printf("\nfinish dealing with the file\n");
	gettimeofday(&end,NULL);
    duration=1000000*(end.tv_sec-begin.tv_sec)+end.tv_usec-begin.tv_usec; 
    printf("The duration for dealing with the file is %lf\n",duration/1000000);
    printf("\n");
	printf("All the subthread ended, now the program is merging its results.\n");
	printf("begin to merge results\n");
	gettimeofday(&begin,NULL);
	ResultSet set=getresult(n);
	printf("The mappings for text.xml is:\n");
	print_result(set);
	printf("finish merging these results.\n");
    gettimeofday(&end,NULL);
    duration=1000000*(end.tv_sec-begin.tv_sec)+end.tv_usec-begin.tv_usec; 
    printf("The duration for merging these results is %lf\n",duration/1000000);
    
    //system("pause");
    return 0;
}
