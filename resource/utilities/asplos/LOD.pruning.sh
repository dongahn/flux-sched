#!/bin/sh

#SBATCH -N 1
#SBATCH -t 16:00:00
#SBATCH -p pbatch
#SBATCH -o /g/g0/dahn/workspace/asplos/flux-sched/resource/utilities/asplos/LOD.out

../resource-query.nopf -G high.LOD.graphml --match-format=pretty_simple -e -P low -d -t high.LOD.out < high.seq
echo "----------- Done: high.LOD.graphml prune-filters=\"\" ---------------"
echo ""
../resource-query.nopf -G med.LOD.graphml --match-format=pretty_simple -e -P low -d -t med.LOD.out < med.seq
echo "----------- Done: med.LOD.graphml prune-filters=\"\" ---------------"
echo ""
../resource-query.nopf -G low.LOD.graphml --match-format=pretty_simple -e -P low -d -t low.LOD.out < low.seq
echo "----------- Done: low.LOD.graphml prune-filters=\"\" ---------------"
echo ""

../resource-query -G high.LOD.graphml --match-format=pretty_simple -e -P low -d -t high.LOD.prune.out < high.seq
echo "----------- Done: high.LOD.graphml prune-filters=ALL:core \"\" ---------------"
echo ""
../resource-query -G med.LOD.graphml --match-format=pretty_simple -e -P low -d -t med.LOD.prune.out < med.seq
echo "----------- Done: med.LOD.graphml prune-filters=ALL:core ---------------"
echo ""
../resource-query -G low.LOD.graphml --match-format=pretty_simple -e -P low -d -t low.LOD.prune.out < low.seq
echo "----------- Done: low.LOD.graphml prune-filters=ALL:core ---------------"
echo ""
../resource-query -G low.LOD2.graphml --match-format=pretty_simple -e -P low -d -t low.LOD2.prune.out < med.seq
echo "----------- Done: low.LOD2.graphml prune-filters=ALL:core ---------------"
echo ""

