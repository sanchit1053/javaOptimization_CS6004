import java.io.IOException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

import soot.*;
public class PA4 {
    public static void callSoot(String args[],AnalysisTransformer analysisTransformer){
        PackManager.v().getPack("wjtp").add(new Transform("wjtp.pfcp", analysisTransformer));
        soot.Main.main(args);
        soot.G.reset();
    }
    public static void main(String[] args) {
        String classPath = "."; 	// change to appropriate path to the test class
        //Set up arguments for Soot
        String tcdir = "./testcases";
        String[] sootArgs = {
            "-cp", classPath, "-pp", // sets the class path for Soot
            "-w","-f","c",//TODO: modify to produce bytecode
            "-keep-line-number", // preserves line numbers in input Java files  
            "-main-class","testcasePlaceholder",
            "testcasePlaceholder2"
        };

        // Create transformer for analysis
        AnalysisTransformer analysisTransformer = new AnalysisTransformer(tcdir);

        // // Add transformer to appropriate pack in PackManager; PackManager will run all packs when soot.Main.main is called
        // // Call Soot's main method with arguments
        // //compile:
        deleteClassFiles(tcdir);
        runShellCommand("javac "+tcdir+"/*.java");
        // //copy testcases to .
        List<Path> copiedFiles = copyFilesFromTo(tcdir,".");
        for(Path file:copiedFiles){
            if(file.toString().endsWith(".class")){
                String parts[] = file.toString().split(Pattern.quote("\\"));
                String fname = parts[parts.length-1];
                parts = fname.toString().split(Pattern.quote("/"));
                fname = parts[parts.length-1];
                
                System.out.println(fname);
                
                String className = fname.split(Pattern.quote("."))[0];
                sootArgs[sootArgs.length-1] = className;
                sootArgs[sootArgs.length-2] = className;
                // if(className.equals("T6")){
                    callSoot(sootArgs,analysisTransformer);
                // }
            }
        }
        deleteFiles(copiedFiles);
        //delete testcases from .

    }

    private static void deleteClassFiles(String tcdir) {
        try (DirectoryStream<Path> directoryStream = Files.newDirectoryStream(Paths.get(tcdir))) {
            for (Path path : directoryStream) {
                if(path.toString().endsWith(".class")){
                    Files.delete(path);
                }
            }
        } catch (IOException e) {
            System.out.println("IOException while deleting class files");
        }
    }

    private static void deleteFiles(List<Path> copiedFiles) {
        for(Path file:copiedFiles){
            try {
                Files.delete(file);
            } catch (IOException e) {
                System.out.println("IO exception while deletion.");
            }
        }
    }

    private static List<Path> copyFilesFromTo(String from,String to) {
        Path sourceDir = Paths.get(from); // Specify the source directory
        Path targetDir = Paths.get(to); // Specify the target directory (current directory)
        List<Path> copiedFiles = new ArrayList<Path>();
        try {
            // Iterate over all files and directories in the source directory
            Files.walk(sourceDir)
                .filter(Files::isRegularFile) // Filter out directories
                .forEach(source -> {
                    try {
                        Path target = targetDir.resolve(sourceDir.relativize(source));
                        Files.copy(source, target, StandardCopyOption.REPLACE_EXISTING);
                        // System.out.println("Copied: " + source + " to " + target);
                        copiedFiles.add(target);
                    } catch (IOException e) {
                        System.err.println("Failed to copy " + source + ": " + e);
                    }
                });
        } catch (IOException e) {
            System.err.println("Failed to walk through directory: " + e);
        }
        return copiedFiles;
    }

    public static int runShellCommand(String cmd){
        ProcessBuilder processBuilder = new ProcessBuilder(cmd.split("\\s+"));
        try{
            Process process = processBuilder.start();
            return process.waitFor();
        }
        catch (IOException e){
            System.out.println("IO Exception with: "+cmd);
        }
        catch (InterruptedException e){
            System.out.println("Don't interrupt me!");
        }
        return -1;
    }
}
