import java.io.File;
import java.util.Scanner;
import java.util.HashMap;
import java.util.*;

public class vmsim{
    static int numFrames; //number of frames get from commandline
    static int refreshRate;//after how many cycle we refresh
    static int pageFault = 0;//number of page fault
    static int diskWrite = 0;//number of times write back to disk
    static int numberOfPages = 0;//number of total pages
    
    static long[] pageNumber = new long[2000000];//array to put page number
    static int[] cycleArray = new int[2000000];//array to put cycles
    static char[] modeArray = new char[2000000];//array to put store('s') or load('l')
    static int timeline = -1;

    static String algorithm;//name of the algorithm
    static String traceFile;//name of the trace file

    
    @SuppressWarnings("unchecked")
    public static void doOpt() throws Exception
    {
        HashSet<Long> set = new HashSet<>();
        HashMap<Long, Boolean> dirtyMap = new HashMap<>();
        for (int i=0; i<numberOfPages; i++){
            long address = pageNumber[i];
            if(modeArray[i]=='s')
                dirtyMap.put(address, true);
            if(set.contains(address))
                continue;
            if(set.size()==numFrames) {
                HashSet<Long> cloned_set;
                cloned_set = (HashSet) set.clone();
                for (int k = i + 1; cloned_set.size() > 1 && k < numberOfPages; k++) {
                    cloned_set.remove(pageNumber[k]);
                }
                //get last iterator
                Iterator<Long> iterator = cloned_set.iterator();
                long victim = iterator.next();
                set.remove(victim);
                if (!dirtyMap.containsKey(victim)) {//if not contains, set it to true
                    dirtyMap.put(victim, false);
                }
                else if (dirtyMap.containsKey(victim)&&(dirtyMap.get(victim)==true)) {//if contains and false
                    dirtyMap.put(victim, false);
                    diskWrite++;
                }
            }
            set.add(address);
            pageFault++;
        }
        display_pageFault("OPT");
    }

    @SuppressWarnings("unchecked")
    public static void doFifo()
    {
        HashSet<Long> set = new HashSet<>();
        Queue<Long> queue = new LinkedList<>();
        HashMap<Long, Boolean> dirtyMap = new HashMap<>();
        for (int i=0; i<numberOfPages; i++){
            long address = pageNumber[i];
            if(modeArray[i]=='s')
                dirtyMap.put(address, true);
            if(set.contains(address))
                continue;
            if(set.size()==numFrames) {
                long victim = queue.peek();
                if(!dirtyMap.containsKey(victim)){
                    dirtyMap.put(victim, false);
                }
                else if (dirtyMap.containsKey(victim)&&(dirtyMap.get(victim)==true)) {//if contains and false
                    dirtyMap.put(victim, false);
                    diskWrite++;
                }
                set.remove(victim);
                queue.poll();
            }
            queue.add(address);
            set.add(address);
            
            pageFault++;
        }
        display_pageFault("FIFO");
    }
    @SuppressWarnings("unchecked")
    //cycle array: how many cycle passed since last iteration
    public static void doAging() throws Exception {
        HashMap<Long, String> pt = new HashMap<>();
        HashMap<Long, Boolean> dirtyMap = new HashMap<>();
        HashMap<Long, Boolean> referenced = new HashMap<>();
        for (int i = 0; i < numberOfPages; i++) {
            long address = pageNumber[i];


            int cycle = cycleArray[i];
            int timeShift = (timeline + cycle + 1) / refreshRate;
            timeline = (timeline + cycle + 1) % refreshRate;
            if (timeShift > 0) {
                for (Map.Entry<Long, String> entry : pt.entrySet()) {
                    for (int k = 0; k < timeShift; k++) {
                        if (referenced.get(entry.getKey()) == true) {
                            entry.setValue(right_shift_by_one_bit(entry.getValue(), 1));
                            referenced.put(entry.getKey(), false);
                        } else {
                            entry.setValue(right_shift_by_one_bit(entry.getValue(), 0));
                        }
                    }
                }
            }
            if (!referenced.containsKey(address))
                referenced.put(address, false);
            else
                referenced.put(address, true);

            if (modeArray[i] == 's')
                dirtyMap.put(address, true);
            if (!pt.containsKey(address)) {
                if (pt.size() == numFrames) {
                    long victim = 0;
                    HashSet<Long> cleanSet = new HashSet<>();
                    HashSet<Long> DirtySet = new HashSet<>();
                    String victim_value = "11111111";
                    for (Map.Entry<Long, String> entry : pt.entrySet()) {
                        if (largerValueBoolean(victim_value, entry.getValue())) {
                            victim = entry.getKey();
                            victim_value = entry.getValue();
                        }
                    }
                    for (Map.Entry<Long, String> entry : pt.entrySet()) {
                        if (entry.getValue().equals(victim_value)) {
                            if((!dirtyMap.containsKey(entry.getKey()))||(dirtyMap.get(entry.getKey())==false))
                            {
                                cleanSet.add(entry.getKey());
                            }
                            else {
                                DirtySet.add(entry.getKey());
                            }
                        }
                    }
                    long smallest_address = 999999999;

                    if(cleanSet.size()>0)
                    {
                        Iterator<Long> iterator = cleanSet.iterator();
                        while(iterator.hasNext()) {
                            long temp = iterator.next();
                            if (temp < smallest_address) {

                                victim = temp;
                                smallest_address = temp;
                            }
                        }
                    }
                    else {
                        Iterator<Long> iterator = DirtySet.iterator();
                        while ((iterator.hasNext())) {
                            long temp = iterator.next();
                            if (temp < smallest_address) {
                                victim = temp;
                                smallest_address = temp;
                            }
                        }
                    }
                        if (!dirtyMap.containsKey(victim)) {
                            dirtyMap.put(victim, false);
                        } else if (dirtyMap.containsKey(victim) && (dirtyMap.get(victim) == true)) {
                            dirtyMap.put(victim, false);
                            diskWrite++;
                        }
                        referenced.remove(victim);
                        pt.remove(victim);

                    }
                    pageFault++;
                }
                if (!pt.containsKey(address)) {
                    pt.put(address, "10000000");
                }
            }
            display_pageFault("AGING");
        }

    @SuppressWarnings("unchecked")
    public static Boolean largerValueBoolean(String val1, String val2){
        Boolean larger = false;
        if(val2.compareTo(val1) == -1){
            larger = true;
        }
        return larger;
    }
    @SuppressWarnings("unchecked")
    public static String right_shift_by_one_bit(String val1, int way){
        if(way==0) {
            String shifted1 = "0" + val1.substring(0, 7);
            return shifted1;
        }
        else {
            String shifted2 = "1" + val1.substring(0, 7);
            return shifted2;
        }
    }
    @SuppressWarnings("unchecked")
    public static String binaryOr(String count){
        if(count.charAt(0) == '0'){
            count = "1" + count.substring(1);
        }
        return count;
    }
    @SuppressWarnings("unchecked")
    public static void display_pageFault(String algorithm)
    {
        System.out.println("Algorithm: " + algorithm);
        System.out.println("Number of frames: " + numFrames);
        System.out.println("Total memory accesses: " + numberOfPages);
        System.out.println("Total page faults: " + pageFault);
        System.out.println("Total writes to disk: " + diskWrite);
    }
    private static void verifyArgs(String[] args){
        
        boolean check = true;
        
        if(args.length == 5){ //No refresh argument
            if(args[0].equals("-n")){
                try{
                    numFrames = Integer.parseInt(args[1]);
                }
                catch(Exception e){
                    check = false;
                }
            }
            else
                check = false;

            if(args[2].equals("-a"))
                algorithm = args[3];
            else 
                check = false;

            traceFile = args[4];

        }

        else if(args.length == 7){ //Arguments with refresh argument
            
            if(args[0].equals("-n"))
                try{
                    numFrames = Integer.parseInt(args[1]);
                }
                catch(Exception e){
                    check = false;
                }
            else
                check = false;

            if(args[2].equals("-a"))
                algorithm = args[3];
            else 
                check = false;

            if(args[4].equals("-r"))
                try{
                    refreshRate = Integer.parseInt(args[5]);
                }
                catch(Exception e){
                    check = false;
                }
            else 
                check = false;

            traceFile = args[6];

        }
        else { //if arguments number does not equal to 5 or 7, reject
            check = false;
        }
        algorithm = algorithm.toUpperCase();
        if(!algorithm.equals("FIFO") && !algorithm.equals("AGING") && !algorithm.equals("OPT"))
            check = false;
        if(!check){
            System.out.println("Invalid Arguments!");
            System.exit(-1);
        }
    }

    private static void readFile() throws Exception{
        File file = new File(traceFile);
        Scanner inScan = new Scanner(file);
        while (inScan.hasNext()) {
            char mode = inScan.next().charAt(0);
            long address = Long.parseLong(inScan.next().substring(2,7), 16);
            int cycle = Integer.parseInt(inScan.next());
            modeArray[numberOfPages] = mode;
            pageNumber[numberOfPages] = address;
            cycleArray[numberOfPages] = cycle;
            numberOfPages++;
        }
    }

    private static void run() throws Exception{
        if(algorithm.equals("FIFO")) doFifo();
        else if(algorithm.equals("OPT")) doOpt();
        else if(algorithm.equals("AGING")) doAging();
    }
    @SuppressWarnings("unchecked")
    public static void main(String[] args) throws Exception{
        
        verifyArgs(args);
        readFile();
        run();
    }
}