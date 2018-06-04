//un/comment this for tracing options
//#define DBGTRACE 1
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include "GBase.h"
#include "GArgs.h"
#include "GStr.h"
#include "GHash.hh"
#include "GList.hh"

#ifdef DBGTRACE
#include <gcl/GShMem.h>
#endif

#define usage "Perform transitive-closure clustering by filtering tabulated hits. Usage:\n\
 tclust [<hits_file>] [-H] [-f <flthits_file>] [-o <out_file>] [-x <xcludelist>]\n\
 [-s <seqlist>] [-r <restrictlst>] [-c <clone_lines>]\n\
 [SEQFLT={ET|EST|ET2EST}] [-x <excludefile>] [SCOV=xx] [LCOV=xx] [SCORE=xx] \n\
 [OVHANG=xx] [OVL=xx] [PID=xx]\n\
 Options:\n\
 -H  : disable the fasta-style header (with node count info) for output clusters\n\
 -o  : write output in <out_file> instead of stdout\n\
 -t  : expects the tabulated hit data format, using only the \n\
       first and fifth fields (when no hit filter is used)\n\
 -f  : write to <flthits_file> all the lines that passed the filters\n\
 \n\
 Filters (optional, can be combined):\n\
 -s use only pairs involving at least one sequence from <seqlist> \n\
 -r use only hits between sequences from list <restrictlst> \n\
 -c use clone sets from file <clone_lines>\n\
 -x load sequence names from <excludefile> and discard any pairs related to them \n\
 SEQFLT=  filter sequence names for specific sequence type clustering: \n\
          EST  - EST-only clustering (use only ESTvsEST hits) \n\
          ET   - ETs/NPs only clustering\n\
          ET2EST - ignore EST-EST pairs, uses only EST-ET or ET-ET pairs\n\
 OVL=xx   minimum overlap (bp) (default=20)\n\
 SCOV=xx  is the minimum coverage of the shorter sequence  \n\
          (minimum overlap length as percentage of the shorter sequence)\n\
 LCOV=xx  is the minimum coverage of the longer sequence  \n\
          (minimum overlap length as percentage of the longer sequence)\n\
 OVHANG=xx   is the maximum overhang (in nucleotides) to be allowed\n\
            for any overlap(default 1000)\n\
 PID=xx   is the minimum percent of identity\n\
 SCORE=xx is the minimum score\n\
 \n\
 If <hits_file> is not given, hits are expected at stdin\n\
 If no filters are used and -t option is not used, the program expects just \n\
 space delimited pairs; otherwise each line must have these tabulated fields:\n\n\
 q_name q_len q_n5 q_n3 hit_name hit_len hit_n5 hit_n3 pid score p-value strand\n\
"
#define BUF_LEN 4096 //maximum input line length 
#define ERR_INVALID_PAIR "Invalid input line encountered:\n%s\n"
//============ structures:
//define a cluster object as a list of node names (GStrs)
int comparePtr(void* p1, void* p2) {
  return (p1>p2) ? -1 : ((p1<p2)?1:0);
  }

class CNode;

class GCluster :public GList<CNode> {
 public:
  bool operator==(GCluster& d){
     return (this==&d);
     }
  bool operator>(GCluster& d){
     return (this>&d);
     }
  bool operator<(GCluster& d){
     return (this<&d);
     }
  GCluster():GList<CNode>(true,false,true) {
   //default is: sorted by Node pointer, don't free nodes, unique
    }
  GCluster(bool sorted, bool free_elements=true, bool beUnique=false):
     GList<CNode>(sorted,free_elements,beUnique) {
       }
  };

class CNode {
 public:
   char* id; //actual node ID
   GCluster* cluster; /* pointer to a list of nodes (the t-cluster
                         containing this sequence) */
   //GCluster links; //list of all nodes linked to this node
   CNode(char* name) {
    //links are sorted, not free, unique
    id=Gstrdup(name);    
    cluster=new GCluster;
    cluster->Add(this);
    }
   CNode(char* name, CNode* pairnode) {
    id=Gstrdup(name);
    cluster=pairnode->cluster; //same cluster
    cluster->Add(this); //add self to the cluster
    }
   /* no need for these for basic transitive closure:
   CNode(char* name):links(true,false,true) {
    //links are sorted, not free, unique
    id=Gstrdup(name);    
    cluster=new GCluster;
    cluster->Add(this);
    }
   CNode(char* name, CNode* pairnode):links(true,false,true) {
    id=Gstrdup(name);
    cluster=pairnode->cluster; //same cluster
    cluster->Add(this); //add self to the cluster
    }
   // links management:
   int linksCount(GCluster* C) {
    // returns the number of links that this node  has within cluster C    
    int cnt=0;
    for (int i=0;i<links.Count();i++) 
       if (C->IndexOf(links[i])>=0) cnt++;
    return cnt;
    }
   */ 
  //comparison: just pointer based:
    bool operator==(CNode& d){
     return (this==&d);
     }
    bool operator>(CNode& d){
     return (this>&d);
     }
    bool operator<(CNode& d){
     return (this<&d);
     }
 };

//-- global hash containing all the nodes
GHash<CNode> all;

#ifdef DBGTRACE
   GShMem mem("tclust", 4096);
   GShMem memsum("tclustsum", 1024);
   GStr slog;
#endif


bool asfasta;
bool tabflt; //flag indicating that tabulated hit lines are expected
bool flt_ET_only=false;
bool flt_EST_only=false;
bool flt_EST2ET=false;
bool flt_Exclude=false; //if an exclusion list was given
bool flt_seqOnly=false;
bool flt_seqRestrict=false;
bool ETcheck=false;
bool do_debug=false;

static char inbuf[BUF_LEN]; // incoming buffer for sequence lines.

FILE* outf;

//all clusters are stored in a unique sorted list
GHash<int> excludeList;
GHash<int> seqonlyList;
GList<GCluster> tclusters(true,true,true);

//=========== functions used:
void addPair(char* n1, char* n2);
int compareCounts(void* p1, void* p2);
int compareID(void* p1, void* p2); //compare node IDs
int getNextValue(char*& p, char* line);
bool seq_filter(char* s1, char* s2); //returns true if the pair can pass through the filter
int readNames(FILE* f, GHash<int>& xhash);
int readClones(FILE* f);

//========================================================
//====================     main      =====================
//========================================================
int main(int argc, char * const argv[]) {
 GArgs args(argc, argv, "htHf:c:o:s:r:x:SEQFLT=PID=SCOV=OVHANG=LCOV=OVL=SCORE=DEBUG=");
 int e;
 if ((e=args.isError())>0)
    GError("%s\nInvalid argument: %s\n", usage, argv[e]);
 if (args.getOpt('h')!=NULL) GError("%s\n", usage);

 GStr infile;
 if (args.startNonOpt()) {
        infile=args.nextNonOpt();
        GMessage("Given file: %s\n",infile.chars());
        }
 asfasta=(args.getOpt('H')==NULL);
 tabflt=(args.getOpt('t')!=NULL);
 do_debug=(args.getOpt("DEBUG")!=NULL);
 int minpid=0, minscov=0, minlcov=0, minovl=20, maxovhang=1000, minscore=0;
 GStr s=args.getOpt('x');
 FILE* fxclude=NULL;
 if (!s.is_empty()) {
   if ((fxclude=fopen(s, "r"))==NULL)
      GError("Cannot open exclusion file '%s'!\n", s.chars());
   int c=readNames(fxclude, excludeList);
   GMessage("Loaded %d sequences to be excluded.\n", c);
   fclose(fxclude);
   flt_Exclude=(c>0);
   }


 FILE* fseqonly=NULL;
 s=args.getOpt('s');
 if (!s.is_empty()) {
   if ((fseqonly=fopen(s, "r"))==NULL)
      GError("Cannot open exclusion file '%s'!\n", s.chars());
   int c=readNames(fseqonly, seqonlyList);
   GMessage("Loaded %d sequence names - only consider pairs including them.\n", c);
   flt_seqOnly=(c>0);
   fclose(fseqonly);
   }
 s=args.getOpt('r');
 if (!s.is_empty()) {
   if ((fseqonly=fopen(s, "r"))==NULL)
      GError("Cannot open exclusion file '%s'!\n", s.chars());
   seqonlyList.Clear();
   int c=readNames(fseqonly, seqonlyList);
   GMessage("Loaded %d sequence names - only consider hits between them.\n", c);
   flt_seqRestrict=(c>0);
   if (flt_seqRestrict) flt_seqOnly=false;
   fclose(fseqonly);
   }

 s=args.getOpt('c');
 if (!s.is_empty()) {
   if ((fxclude=fopen(s, "r"))==NULL)
      GError("Cannot open clone list file '%s'!\n", s.chars());
   int c=readClones(fxclude);
   GMessage("Loaded %d clones.\n", c);
   fclose(fxclude);
   }
 
 s=args.getOpt("SEQFLT");
 if (!s.is_empty()) {
   if (s=="ET") {
     flt_ET_only=true;
     s=" ETs/NPs only (discard ESTs)";
     }
   else if (s=="EST") {
     flt_EST_only=true;
     s=" EST only (discard ETs/NPs)";
     }
   else if (s=="EST2ET" || s=="ET2EST")  {
     flt_EST2ET=true;
     s=" links to ETs/NPs only (discard EST-EST links)";
     }
   else GError("%s\nIncorrect SEQFLT option ('%s' not recognized)", s.chars());
   GMessage("Using sequence filter: %s\n", s.chars());
   ETcheck=true;
   }
 s=args.getOpt("PID");
 if (!s.is_empty()) {
    tabflt=true;
    minpid = s.asInt();
    if (minpid>0) GMessage("PID=%d\n",  minpid);
   }
 s=args.getOpt("SCOV");   
 if (!s.is_empty()) {
    tabflt=true;
    minscov = s.asInt();
    if (minscov>0) GMessage("SCOV=%d\n",  minscov);
   }

 s=args.getOpt("OVHANG");
 if (!s.is_empty()) {
    tabflt=true;
    maxovhang = s.asInt();
   }
 if (maxovhang<1000) GMessage("OVHANG=%d\n",  maxovhang);   

 s=args.getOpt("OVL");
 if (!s.is_empty()) {
    tabflt=true;
    minovl = s.asInt();
   }
 if (minovl>0) GMessage("OVL=%d\n",  minovl);   

 s=args.getOpt("LCOV");
 if (!s.is_empty()) {
    tabflt=true;
    minlcov = s.asInt();
    if (minlcov>0) GMessage("LCOV=%d\n",  minlcov);
   }
 s=args.getOpt("SCORE");   
 if (!s.is_empty()) {
    tabflt=true;
    minscore = s.asInt();
    if (minscore>0) GMessage("SCORE=%d\n",  minscore);
   }
 
 //==
 FILE* inf;
 if (!infile.is_empty()) {
    inf=fopen(infile, "r");
    if (inf==NULL)
       GError("Cannot open input file %s!\n",infile.chars());
    }
  else
   inf=stdin;
   
 GStr pfile=args.getOpt('f'); //write filtered pairs here
 FILE* fpairs=NULL;
 if (!pfile.is_empty()) {
   if (pfile == "-") 
      fpairs=stdout;
    else 
     if ((fpairs=fopen(pfile, "w"))==NULL)
      GError("Cannot write filtered hits file '%s'!", pfile.chars());
   }
  
 //======== main program loop
 char* line;
 if (tabflt) {
    while ((line=fgets(inbuf, BUF_LEN-1,inf))!=NULL) {
        int l=strlen(line);
        if (line[l-1]=='\n') line[l-1]='\0';
        GStr linecpy(line);
        if (strlen(line)<=1) continue;
        char* tabpos=line;
        //find the 1st tab
        while (*tabpos != '\t' && *tabpos!='\0') tabpos++;
        if (*tabpos=='\0' || tabpos==line)
            GError(ERR_INVALID_PAIR, line);
        *tabpos='\0'; //so line would be the first node name
        if (flt_Exclude && excludeList.hasKey(line)) continue;
        tabpos++; //tabpos is now on the first char of the second field (q_len)
        int score, scov, ovh_r, ovh_l, lcov, pid;
        //skip 3 other tabs delimited
        //read the query length:
        int qlen=getNextValue(tabpos, line);
        tabpos++;
        int q5=getNextValue(tabpos,line);
        tabpos++;
        int q3=getNextValue(tabpos,line);
        tabpos++;
        if (q5==q3) GError(ERR_INVALID_PAIR, line);
        bool minus=false;
        if (q5>q3) {
          Gswap(q5,q3);
          minus=true;
          }

        //now we should be on the first char of the hitname field
        while (isspace(*tabpos)) tabpos++; //skip any spaces in this second node name field
        if (*tabpos=='\0') GError(ERR_INVALID_PAIR, line);
        //add a string termination after this field
        char* p=tabpos; while (!isspace(*p) && *p!='\0') p++;
        *p='\0';
        //now tabpos contains the exact second sequence string
        if (flt_seqOnly && seqonlyList.Find(line)==NULL && seqonlyList.Find(tabpos)==NULL)
             continue;
        if (flt_seqRestrict && (seqonlyList.Find(line)==NULL || seqonlyList.Find(tabpos)==NULL))
             continue;
             
        if (strcmp(line, tabpos)==0) {
          GMessage("Warning: self pairing found for node %s\n",line);
          continue;
          }
        if (flt_Exclude && excludeList.hasKey(tabpos)) continue;
        if (!seq_filter(line, tabpos)) continue;
        p++; //move on the first char of the hitlen 
        int hitlen=getNextValue(p,line);
        p++;
        int h5=getNextValue(p,line);
        p++;
        int h3=getNextValue(p,line);
        p++;
        if (h5==h3) GError(ERR_INVALID_PAIR, line);
        if (h5>h3) {
          Gswap(h5,h3);
          minus=!minus;
          }
        pid=getNextValue(p,line); p++;
        score=getNextValue(p,line);
        //compute coverages:
        ovh_r=minus ?(GMIN(q5-1, hitlen-h3)) :(GMIN(hitlen-h3, qlen-q3));
        ovh_l=minus ?(GMIN(h5-1, qlen-q3)) :(GMIN(h5-1, q5-1));
        int overlap = GMAX(q3-q5+1, h3-h5+1);
        if (hitlen>qlen) { //query is shorter
          scov = (int) rint(((double)(q3-q5)*100)/qlen);
          lcov = (int) rint(((double)(h3-h5)*100)/hitlen);
          }
        else {
          lcov = (int) rint(((double)(q3-q5)*100)/qlen);
          scov = (int) rint(((double)(h3-h5)*100)/hitlen);
          }
          
        if (scov>=minscov && lcov>=minlcov && pid>=minpid && overlap>=minovl
            && score>=minscore && ovh_r <= maxovhang && ovh_l <= maxovhang) {
           //GMessage("%s(%d) %d-%d  | %s(%d) %d-%d, pid=%d%%, score=%d, scov=%d%%, lcov=%d%%\n",
           //          line,qlen,q5,q3,tabpos,hitlen, h5,h3,pid, score, scov, lcov);
           if (fpairs!=NULL) fprintf(fpairs, "%s\n", linecpy.chars());
           //if (fpairs!=NULL) 
           //   fprintf(fpairs, "%s\t%s\t%d\t%d\n",line, tabpos, ovh_l, ovh_r);
           addPair(line, tabpos);
           }
      } //while
     } //tabulated hits case
  else {//pairs only:  
     while ((line=fgets(inbuf, BUF_LEN-1,inf))!=NULL) {
      line[BUF_LEN-1]='\0';
      int l=strlen(line);
      if (line[l-1]=='\n') line[l-1]='\0';
      GStr linecpy(line);
      if (strlen(line)<=1) continue;
      char* tabpos=line;
      while (!isspace(*tabpos) && *tabpos!='\0') tabpos++; 
      if (*tabpos=='\0' || tabpos==line)
          GError(ERR_INVALID_PAIR, line);
      *tabpos='\0';
      if (flt_Exclude && excludeList.hasKey(line)) continue;
      tabpos++;
      while (isspace(*tabpos)) tabpos++;
      if (*tabpos=='\0') GError(ERR_INVALID_PAIR, line);
      char *c; //extra check here, to avoid stupid mistakes..
      if ((c=strchr(tabpos, '\t'))!=NULL && strchr(c+1, '\t')!=NULL)
         GError("The incoming stream seem to not be just pairs: \n%s\t%s\n", 
             line, tabpos);
      if (strcmp(line, tabpos)==0) {
        GMessage("Warning: self pairing found for node %s\n",line);
        continue;
        }
      if (flt_seqOnly && seqonlyList.Find(line)==NULL && seqonlyList.Find(tabpos)==NULL)
             continue;
      if (flt_seqRestrict && (seqonlyList.Find(line)==NULL || seqonlyList.Find(tabpos)==NULL))
             continue;
      if (flt_Exclude && excludeList.hasKey(tabpos)) continue;
      if (!seq_filter(line, tabpos)) continue;
      addPair(line, tabpos);
      if (fpairs!=NULL) fprintf(fpairs, "%s\n", linecpy.chars());
      }
    }
    
  if (inf!=stdin) fclose(inf);
  if (fpairs!=NULL && fpairs!=stdout) fclose(fpairs);
  //==========
  //
  GStr outfile=args.getOpt('o');
  if (!outfile.is_empty()) {
     outf=fopen(outfile, "w");
     if (outf==NULL)
        GError("Cannot open file %s for writing!\n",outfile.chars());
     }
   else outf=stdout;
  GMessage("Total t-clusters: %d \n", tclusters.Count());
  tclusters.setSorted(&compareCounts);
  if (tclusters.Count()>0) {
     GMessage("Largest cluster has %d nodes\n", tclusters[0]->Count());
     for (int i=0; i<tclusters.Count(); i++) {
        GCluster* c=tclusters[i];
        c->setSorted(&compareID);
        if (asfasta) fprintf(outf,">CL%d\t%d\n", i+1, c->Count());
        CNode* n=c->Get(0);fprintf(outf,"%s",n->id);
        for (int j=1; j<c->Count();j++)
          fprintf(outf,"\t%s",c->Get(j)->id);
        fprintf(outf,"\n");
        }
     fflush(outf);
     }
  all.Clear(); //this frees the nodes
  tclusters.Clear(); /* this frees the clusters, 
     but should not try to free the nodes inside */
  //GMessage("the tclusters list was cleared!\n");
  if (outf!=stdout) fclose(outf);
  GMessage("*** all done ***\n");
  //getc(stdin);
}

//
int getNextValue(char*& p, char* line) {
 //expects p to be on the first char of a numeric field
 char* start, *end;
 while (isspace(*p) && *p!='\0') p++; //skip any spaces;
 if (*p=='\0') GError(ERR_INVALID_PAIR, line);
 start=p;
 while (*p!='\t' && *p!='\0') p++;
 //now p should be on the next tab or eos;
 end=p-1;
 while (isspace(*end)) end--;
 //char c=*(end+1);
 *(end+1)='\0';
 double d=atof(start);
 return (int)rint(d);
 //backup
 }
//=====
int compareCounts(void* p1, void* p2) {
 int c1=((GCluster*)p1)->Count();
 int c2=((GCluster*)p2)->Count();
 return (c1>c2)?-1:((c1<c2)?1:0);
}

//=====
int compareID(void* p1, void* p2) {
 return strcmp(((CNode*)p1)->id,((CNode*)p2)->id);
}


//-----
//both nodes are new - create global entries for them
// and a new shared t-cluster containing them 
void newPair(char* id1, char* id2, CNode*& n1, CNode*& n2) {
 n1=new CNode(id1); /* this creates a new cluster with only self in it */
 n2=new CNode(id2, n1); //d2->cluster set to d1->cluster
 //d1->cluster->Add(d2); //second CNode added to the cluster
 all.shkAdd(n1->id,n1);
 all.shkAdd(n2->id,n2);
 //one cluster created: add it to the global list of t-clusters:
 tclusters.Add(n1->cluster);
}

//add a new node with name id1, paired with an existing node n2
CNode* addNode(char* id1, CNode* n2) {
 //use the same cluster
 CNode* n1=new CNode(id1,n2); //this automatically adds n1 to the shared t-cluster
 all.shkAdd(n1->id,n1);
 return n1;
}

//create a link between two existing nodes
void linkNodes(CNode* n1, CNode* n2) {
if (n1->cluster==n2->cluster) return;
//we have to merge the two clusters
//add to the bigger cluster the sequences from the smaller one
//(big fish swallows the smaller one)
GCluster* src, *dest;
if (n1->cluster->Count()>n2->cluster->Count()) {
  // n1 is the bigger fish
  dest = n1->cluster;
  src  = n2->cluster;
  }
else {
  dest = n2->cluster;
  src  = n1->cluster;
  }
//merge the clusters (in the bigger one)
//and remove the smaller cluster  
for (register int i=0; i<src->Count(); i++) {
     CNode* n= src->Get(i);
     dest->Add(n);  /* check duplicates? they should never be there!
          if (dest->Add(n)<0)  {
                   delete n;
                   src->Put(i,NULL);
                   } */
     //each node from smaller cluster should now point to the bigger (current)cluster
     n->cluster=dest;
     }
  src->setFreeItem(false);
  tclusters.Remove(src);
}


bool isET(char* name) {
 return (startsWith(name, "np|") ||
        startsWith(name, "et|") || 
        startsWith(name, "egad|") ||
        startsWith(name, "preegad|")
        );
 }

bool seq_filter(char* s1, char* s2) {
 bool isET1, isET2;
 if (ETcheck) {
   isET1=isET(s1);
   isET2=isET(s2);
   if (flt_ET_only) 
     return (isET1 && isET2); //both are et
   else if (flt_EST_only)
     return (!isET1 && !isET2);
   else if (flt_EST2ET)
     return (isET1 || isET2);
   }
 return true; //passed
 }
 
 
/* addPair() should slowly build the t-clusters and update
   the links for both n1 and n2
*/
void addPair(char* s1, char* s2) {
 CNode* n1=all.Find(s1);
 CNode* n2=all.Find(s2); 
 //GMessage("Add pair %s %s\n",n1,n2);
 if (n1==NULL) {
   //###### n1 is new
   if (n2==NULL) //n2 is also new 
        newPair(s1,s2, n1, n2);
          //this should set d1 and d2 to the each new CNode*
      else //n1 is new, but n2 already has a t-cluster
        n1 = addNode(s1,n2);
   }
  else {
   //###### n1 has a t-cluster
   if (n2==NULL) //n2 is new
        n2=addNode(s2,n1);
      else //n2 also has a t-cluster, merge them
        linkNodes(n1,n2);
   }
}

void showCl(GCluster* C) {
  GMessage("{%s",C->Get(0)->id);
  for (int i=1;i<C->Count();i++) 
     GMessage(" %s",C->Get(i)->id);
  GMessage("}");
}


#ifdef DBGTRACE
void memlog(int level, const char* msg1, 
         GCluster* C=NULL, 
         const char* msg2=NULL) {
 GStr s;
 if (level>0) {
   s.format("%d>", level);
   s.padR(level+2);
   }
 if (msg1!=NULL) s+=msg1;
 if (C!=NULL) {
     GStr s2;
     s2.format("(%d){%s",C->Count(), C->Get(0)->id);
     s+=s2;
     for (int i=1;i<C->Count();i++)  {
        s2.format(" %s",C->Get(i)->id);
        s+=s2;
        }
     s+="}";
     }
 if (msg2!=NULL) s+=msg2;
 mem.log(s.chars()); 
 }
#endif  


int readNames(FILE* f, GHash<int>& xhash) {
  int c;
  int count=0;
  char name[256]; 
  int len=0;
  while ((c=getc(f))!=EOF) {
    if (isspace(c)) {
      if (len>0) {
        name[len]='\0';      
        xhash.Add(name, new int(1));
        count++;
        len=0;
        }
      continue;  
      }
    //a non-space
    name[len]=(char) c;
    len++;
    if (len>255) {
      name[len-1]='\0';
      GError("Error reading file: name too long ('%s') !\n",name);      
      }
    }
 return count;   
}

int readClones(FILE* f) {
//expects space delimited reads, one clone per line
  char* line;
  int count=0;
  while ((line=fgets(inbuf, BUF_LEN-1,f))!=NULL) {
   int l=strlen(line);
   count++;
   if (line[l-1]=='\n') line[l-1]='\0';
   GStr linestr(line);
   linestr.startTokenize("\t ");
   GStr token;
   CNode* head=NULL;
   while (linestr.nextToken(token)) {
       if (flt_seqRestrict && !seqonlyList.Find(token.chars()))
          continue;
       if (head==NULL) {
         head=new CNode((char *)token.chars());
         tclusters.Add(head->cluster); 
         all.shkAdd(head->id,head);
         /*if (do_debug)
           GMessage("Added head node '%s'\n",head->id);*/
         }
        else 
         addNode((char *)token.chars(), head);      
     }
   }
 return count;
}
